#pragma once

#include <Core/World/SettingsComponent.h>
#include <Core/World/SettingsComponentManager.h>
#include <RendererCore/Lights/Implementation/ReflectionProbeData.h>
#include <RendererCore/Pipeline/RenderData.h>

struct ezMsgUpdateLocalBounds;
struct ezMsgExtractRenderData;
class ezAbstractObjectNode;

  /// @brief
class EZ_RENDERERCORE_DLL ezSphereReflectionProbeComponentManager final : public ezComponentManager<class ezSphereReflectionProbeComponent, ezBlockStorageType::Compact>
{
public:
  ezSphereReflectionProbeComponentManager(ezWorld* pWorld);

  virtual void Initialize() override;

private:
  friend class ezSphereReflectionProbeComponent;
};

  /// @brief
class EZ_RENDERERCORE_DLL ezBoxReflectionProbeComponentManager final : public ezComponentManager<class ezBoxReflectionProbeComponent, ezBlockStorageType::Compact>
{
public:
  ezBoxReflectionProbeComponentManager(ezWorld* pWorld);

  virtual void Initialize() override;

private:
  friend class ezBoxReflectionProbeComponent;
};

/// @brief
class EZ_RENDERERCORE_DLL ezReflectionProbeRenderData : public ezRenderData
{
  EZ_ADD_DYNAMIC_REFLECTION(ezReflectionProbeRenderData, ezRenderData);

public:
  ezReflectionProbeRenderData()
  {
    m_Id.Invalidate();
    m_vHalfExtents.SetZero();
  }

  ezReflectionProbeId m_Id;
  ezUInt32 m_uiIndex = 0;
  ezVec3 m_vHalfExtents;
  ezVec3 m_vPositiveFalloff;
  ezVec3 m_vNegativeFalloff;
};

struct EZ_RENDERERCORE_DLL ezReflectionProbeDesc
{
  ezUuid m_uniqueID;

  ezTagSet m_IncludeTags;
  ezTagSet m_ExcludeTags;

  ezEnum<ezReflectionProbeMode> m_Mode;

  bool m_bShowDebugInfo = false;
  float m_fIntensity = 1.0f;
  float m_fSaturation = 1.0f;
};

/// @brief
class EZ_RENDERERCORE_DLL ezReflectionProbeComponent : public ezComponent
{
  EZ_ADD_DYNAMIC_REFLECTION(ezReflectionProbeComponent, ezComponent);

public:
  ezReflectionProbeComponent();
  ~ezReflectionProbeComponent();

  void SetReflectionProbeMode(ezEnum<ezReflectionProbeMode> mode); // [ property ]
  ezEnum<ezReflectionProbeMode> GetReflectionProbeMode() const;    // [ property ]

  void SetIntensity(float fIntensity); // [ property ]
  float GetIntensity() const;          // [ property ]

  void SetSaturation(float fSaturation); // [ property ]
  float GetSaturation() const;           // [ property ]

  const ezTagSet& GetIncludeTags() const;   // [ property ]
  void InsertIncludeTag(const char* szTag); // [ property ]
  void RemoveIncludeTag(const char* szTag); // [ property ]

  const ezTagSet& GetExcludeTags() const;   // [ property ]
  void InsertExcludeTag(const char* szTag); // [ property ]
  void RemoveExcludeTag(const char* szTag); // [ property ]

  void SetShowDebugInfo(bool bShowDebugInfo); // [ property ]
  bool GetShowDebugInfo() const;              // [ property ]

  //////////////////////////////////////////////////////////////////////////
  // ezComponent

public:
  virtual void SerializeComponent(ezWorldWriter& stream) const override;
  virtual void DeserializeComponent(ezWorldReader& stream) override;

protected:
  ezReflectionProbeDesc m_desc;

  ezReflectionProbeId m_Id;
  // Set to true if a change was made that requires recomputing the cube map.
  mutable bool m_bStatesDirty = true;
};

//////////////////////////////////////////////////////////////////////////
// ezSphereReflectionProbeComponent

class EZ_RENDERERCORE_DLL ezSphereReflectionProbeComponent : public ezReflectionProbeComponent
{
  EZ_DECLARE_COMPONENT_TYPE(ezSphereReflectionProbeComponent, ezReflectionProbeComponent, ezSphereReflectionProbeComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // ezComponent

public:
  virtual void SerializeComponent(ezWorldWriter& stream) const override;
  virtual void DeserializeComponent(ezWorldReader& stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;


  //////////////////////////////////////////////////////////////////////////
  // ezSphereReflectionProbeComponent

public:
  ezSphereReflectionProbeComponent();
  ~ezSphereReflectionProbeComponent();

  void SetRadius(float fRadius); // [ property ]
  float GetRadius() const;       // [ property ]

  void SetFalloff(float fFalloff);                // [ property ]
  float GetFalloff() const { return m_fFalloff; } // [ property ]

protected:
  //////////////////////////////////////////////////////////////////////////
  // Editor
  void OnObjectCreated(const ezAbstractObjectNode& node);

protected:
  void OnUpdateLocalBounds(ezMsgUpdateLocalBounds& msg);
  void OnMsgExtractRenderData(ezMsgExtractRenderData& msg) const;

  float m_fRadius = 5.0f;
  float m_fFalloff = 0.1f;
};

//////////////////////////////////////////////////////////////////////////
// ezBoxReflectionProbeComponent

class EZ_RENDERERCORE_DLL ezBoxReflectionProbeComponent : public ezReflectionProbeComponent
{
  EZ_DECLARE_COMPONENT_TYPE(ezBoxReflectionProbeComponent, ezReflectionProbeComponent, ezBoxReflectionProbeComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // ezComponent

public:
  virtual void SerializeComponent(ezWorldWriter& stream) const override;
  virtual void DeserializeComponent(ezWorldReader& stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;


  //////////////////////////////////////////////////////////////////////////
  // ezSphereReflectionProbeComponent

public:
  ezBoxReflectionProbeComponent();
  ~ezBoxReflectionProbeComponent();

  const ezVec3& GetExtents() const;       // [ property ]
  void SetExtents(const ezVec3& extents); // [ property ]

  void SetPositiveFalloff(const ezVec3& vFalloff);                        // [ property ]
  const ezVec3& GetPositiveFalloff() const { return m_vPositiveFalloff; } // [ property ]
  void SetNegativeFalloff(const ezVec3& vFalloff);                        // [ property ]
  const ezVec3& GetNegativeFalloff() const { return m_vNegativeFalloff; } // [ property ]
 

protected:
  //////////////////////////////////////////////////////////////////////////
  // Editor
  void OnObjectCreated(const ezAbstractObjectNode& node);

protected:
  void OnUpdateLocalBounds(ezMsgUpdateLocalBounds& msg);
  void OnMsgExtractRenderData(ezMsgExtractRenderData& msg) const;

  ezVec3 m_vExtents = ezVec3(5.0f);
  ezVec3 m_vPositiveFalloff = ezVec3(0.0f);
  ezVec3 m_vNegativeFalloff = ezVec3(0.0f);
};
