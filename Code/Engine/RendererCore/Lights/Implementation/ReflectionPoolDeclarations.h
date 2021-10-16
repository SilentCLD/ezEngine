#include <Core/World/World.h>
#include <Foundation/Types/Bitflags.h>
#include <RendererCore/Lights/Implementation/ReflectionPool.h>
#include <RendererCore/Lights/Implementation/ReflectionProbeData.h>
#include <RendererCore/Lights/SkyLightComponent.h>
#include <RendererCore/Lights/SphereReflectionProbeComponent.h>
#include <RendererCore/Pipeline/View.h>

static const ezUInt32 s_uiReflectionCubeMapSize = 128;
static const ezUInt32 s_uiNumReflectionProbeCubeMaps = 32;
static const float s_fDebugSphereRadius = 0.3f;

inline ezUInt32 GetMipLevels()
{
  return ezMath::Log2i(s_uiReflectionCubeMapSize) - 1; // only down to 4x4
}

struct ReflectionView
{
  ezViewHandle m_hView;
  ezCamera m_Camera;
};

struct UpdateStep
{
  typedef ezUInt8 StorageType;

  enum Enum
  {
    RenderFace0,
    RenderFace1,
    RenderFace2,
    RenderFace3,
    RenderFace4,
    RenderFace5,
    Filter,

    ENUM_COUNT,

    Default = Filter
  };

  static bool IsRenderStep(Enum value) { return value >= UpdateStep::RenderFace0 && value <= UpdateStep::RenderFace5; }
  static Enum NextStep(Enum value) { return static_cast<UpdateStep::Enum>((value + 1) % UpdateStep::ENUM_COUNT); }
};


struct DynamicUpdate
{
  bool operator==(const DynamicUpdate& b) const
  {
    return m_Id == b.m_Id && m_uiWorldIndex == b.m_uiWorldIndex;
  }

  ezUInt32 m_uiWorldIndex = 0;
  ezReflectionProbeId m_Id;
};
EZ_CHECK_AT_COMPILETIME(sizeof(DynamicUpdate) == 8);

template <>
struct ezHashHelper<DynamicUpdate>
{
  EZ_ALWAYS_INLINE static ezUInt32 Hash(DynamicUpdate value) { return ezHashHelper<ezUInt64>::Hash(reinterpret_cast<ezUInt64&>(value)); }

  EZ_ALWAYS_INLINE static bool Equal(DynamicUpdate a, DynamicUpdate b) { return a.m_Id == b.m_Id && a.m_uiWorldIndex == b.m_uiWorldIndex; }
};


//////////////////////////////////////////////////////////////////////////
/// ProbeUpdateInfo

struct ProbeUpdateInfo
{
  ProbeUpdateInfo();
  ~ProbeUpdateInfo();
  void Reset();
  void Setup(DynamicUpdate target);

  DynamicUpdate m_UpdateTarget;
  ezEnum<ezReflectionProbeMode> m_Mode;
  bool m_bComputeIrradiance = false;

  struct Step
  {
    EZ_DECLARE_POD_TYPE();

    ezUInt8 m_uiViewIndex;
    ezEnum<UpdateStep> m_UpdateStep;
  };

  bool m_bInUse = false;
  bool m_bCalledThisFrame = false;
  ezEnum<UpdateStep> m_LastUpdateStep;

  ezHybridArray<Step, 8> m_UpdateSteps;

  ezGALTextureHandle m_hCubemap;
  ezGALTextureHandle m_hCubemapProxies[6];
};

//////////////////////////////////////////////////////////////////////////
/// SortedUpdateInfo

struct SortedUpdateInfo
{
  EZ_DECLARE_POD_TYPE();

  EZ_ALWAYS_INLINE bool operator<(const SortedUpdateInfo& other) const
  {
    if (m_fPriority > other.m_fPriority) // we want to sort descending (higher priority first)
      return true;

    return m_uiIndex < other.m_uiIndex;
  }

  ProbeUpdateInfo* m_pUpdateInfo = nullptr;
  ezUInt32 m_uiIndex = 0;
  float m_fPriority = 0.0f;
};

// must not be in anonymous namespace
template <>
struct ezHashHelper<ezReflectionProbeId>
{
  EZ_ALWAYS_INLINE static ezUInt32 Hash(ezReflectionProbeId value) { return ezHashHelper<ezUInt32>::Hash(value.m_Data); }

  EZ_ALWAYS_INLINE static bool Equal(ezReflectionProbeId a, ezReflectionProbeId b) { return a == b; }
};


struct SortedProbes
{
  EZ_DECLARE_POD_TYPE();

  EZ_ALWAYS_INLINE bool operator<(const SortedProbes& other) const
  {
    if (m_fPriority > other.m_fPriority) // we want to sort descending (higher priority first)
      return true;

    return m_uiIndex < other.m_uiIndex;
  }

  ezReflectionProbeId m_uiIndex;
  float m_fPriority = 0.0f;
};

EZ_DECLARE_FLAGS(ezUInt8, ezProbeFlags, SkyLight, HasCustomCubeMap, Sphere, Box, Dirty, Dynamic, Usable);
EZ_DECLARE_REFLECTABLE_TYPE(EZ_RENDERERCORE_DLL, ezProbeFlags);

struct ProbeData
{
  // Descriptor
  ezReflectionProbeDesc m_desc;
  // Component Settings
  ezTransform m_GlobalTransform;

  ezBitflags<ezProbeFlags> m_Flags;

  ezVec3 m_vHalfExtents;
  // Runtime data
  ezTextureCubeResourceHandle m_hCubeMap; // static data or empty for dynamic.
  ezInt32 m_uiReflectionIndex = -1;
  float m_fPriority = 0.0f;
  ezInt32 m_uiIndexInUpdateQueue = -1;
};

//////////////////////////////////////////////////////////////////////////
/// ezReflectionPool::Data


struct ezReflectionPool::Data
{
  Data();
  ~Data();

  struct WorldReflectionData
  {
    WorldReflectionData()
    {
      m_MappedCubes.SetCount(s_uiNumReflectionProbeCubeMaps);
    }

    ezIdTable<ezReflectionProbeId, ProbeData> m_Probes;
    ezReflectionProbeId m_SkyLight; // SkyLight is always fixed at reflectionIndex 0.

    ezGALTextureHandle m_hReflectionSpecularTexture;
    ezUInt32 m_uiRegisteredProbeCount = 0;

    // Mapping
    ezStaticArray<ezReflectionProbeId, s_uiNumReflectionProbeCubeMaps> m_MappedCubes;

    // Cleared every frame:
    ezDynamicArray<SortedProbes> m_SortedProbes;                                 // All probes exiting in the scene, sorted by priority.
    ezHybridArray<SortedProbes, s_uiNumReflectionProbeCubeMaps> m_ActiveProbes;  // Probes that are currently mapped in the atlas.
    ezHybridArray<ezInt32, s_uiNumReflectionProbeCubeMaps> m_UnusedProbeSlots;   // Probe slots are are currently unused in the atlas.
    ezHybridArray<SortedProbes, s_uiNumReflectionProbeCubeMaps> m_AddProbes; // Probes that should be added to the atlas
  };

  // WorldReflectionData management
  ezReflectionProbeId AddProbe(const ezWorld* pWorld, ProbeData&& probeData);
  ezReflectionPool::Data::WorldReflectionData& GetWorldData(const ezWorld* pWorld);
  void RemoveProbe(const ezWorld* pWorld, ezReflectionProbeId id);
  bool UpdateProbeData(ProbeData& probeData, const ezReflectionProbeDesc& desc, const ezReflectionProbeComponent* pComponent);
  bool UpdateSkyLightData(ProbeData& probeData, const ezReflectionProbeDesc& desc, const ezSkyLightComponent* pComponent);
  void MapProbe(const ezUInt32 uiWorldIndex, ezReflectionPool::Data::WorldReflectionData& data, ezReflectionProbeId id, ezInt32 iReflectionIndex);
  void UnmapProbe(const ezUInt32 uiWorldIndex, ezReflectionPool::Data::WorldReflectionData& data, ezReflectionProbeId id);

  // Dynamic Update Queue
  void PreExtraction();
  void PostExtraction();
  void GenerateUpdateSteps();

  ezHashSet<DynamicUpdate> m_PendingDynamicUpdate;
  ezDeque<DynamicUpdate> m_DynamicUpdateQueue;

  // Dynamic Updates
  ezDynamicArray<ReflectionView> m_RenderViews;
  ezDynamicArray<ReflectionView> m_FilterViews;
  ezHybridArray<ProbeUpdateInfo, 2> m_DynamicUpdates;

  void CreateWorldReflectionData(ezReflectionPool::Data::WorldReflectionData& data);
  void DestroyWorldReflectionData(ezReflectionPool::Data::WorldReflectionData& data);
  void CreateReflectionViewsAndResources();
  void CreateSkyIrradianceTexture();
  void AddViewToRender(const ProbeUpdateInfo::Step& step, const ProbeData& probeData, ProbeUpdateInfo& updateInfo, const ezTransform& transform);

  ezMutex m_Mutex;
  ezUInt64 m_uiWorldHasSkyLight = 0;
  ezUInt64 m_uiSkyIrradianceChanged = 0;

  // GPU storage
  ezHybridArray<WorldReflectionData, 64> m_hReflectionSpecularTexture;
  ezGALTextureHandle m_hFallbackReflectionSpecularTexture;
  ezGALTextureHandle m_hSkyIrradianceTexture;
  ezHybridArray<ezAmbientCube<ezColorLinear16f>, 64> m_SkyIrradianceStorage;

  // Debug data
  ezMeshResourceHandle m_hDebugSphere;
  ezHybridArray<ezMaterialResourceHandle, 6 * s_uiNumReflectionProbeCubeMaps> m_hDebugMaterial;
};
