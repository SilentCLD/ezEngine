#include <RendererCore/RendererCorePCH.h>

#include <Core/Graphics/Camera.h>
#include <Core/Graphics/Geometry.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Configuration/Startup.h>
#include <Foundation/Math/Color16f.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/GPUResourcePool/GPUResourcePool.h>
#include <RendererCore/Lights/Implementation/ReflectionPool.h>
#include <RendererCore/Lights/Implementation/ReflectionPoolDeclarations.h>
#include <RendererCore/Lights/Implementation/ReflectionProbeData.h>
#include <RendererCore/Meshes/MeshComponentBase.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererCore/Textures/TextureCubeResource.h>
#include <RendererFoundation/CommandEncoder/ComputeCommandEncoder.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Device/Pass.h>
#include <RendererFoundation/Profiling/Profiling.h>
#include <RendererFoundation/Resources/Texture.h>

// clang-format off
EZ_BEGIN_SUBSYSTEM_DECLARATION(RendererCore, ReflectionPool)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "Foundation",
    "Core",
    "RenderWorld"
  END_SUBSYSTEM_DEPENDENCIES

  ON_HIGHLEVELSYSTEMS_STARTUP
  {
    ezReflectionPool::OnEngineStartup();
  }

  ON_HIGHLEVELSYSTEMS_SHUTDOWN
  {
    ezReflectionPool::OnEngineShutdown();
  }

EZ_END_SUBSYSTEM_DECLARATION;
// clang-format on

ezCVarInt cvar_RenderingReflectionPoolMaxRenderViews("Rendering.ReflectionPool.MaxRenderViews", 1, ezCVarFlags::Default, "The maximum number of render views for reflection probes each frame");
ezCVarInt cvar_RenderingReflectionPoolMaxFilterViews("Rendering.ReflectionPool.MaxFilterViews", 1, ezCVarFlags::Default, "The maximum number of filter views for reflection probes each frame");

//////////////////////////////////////////////////////////////////////////
/// ProbeUpdateInfo

ProbeUpdateInfo::ProbeUpdateInfo()
{
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  {
    ezGALTextureCreationDescription desc;
    desc.m_uiWidth = s_uiReflectionCubeMapSize;
    desc.m_uiHeight = s_uiReflectionCubeMapSize;
    desc.m_uiMipLevelCount = GetMipLevels();
    desc.m_Format = ezGALResourceFormat::RGBAHalf;
    desc.m_Type = ezGALTextureType::TextureCube;
    desc.m_bCreateRenderTarget = true;
    desc.m_bAllowDynamicMipGeneration = true;
    desc.m_ResourceAccess.m_bReadBack = true;
    desc.m_ResourceAccess.m_bImmutable = false;

    m_hCubemap = ezGPUResourcePool::GetDefaultInstance()->GetRenderTarget(desc);
    pDevice->GetTexture(m_hCubemap)->SetDebugName("Reflection Cubemap");
  }

  ezStringBuilder sName;
  for (ezUInt32 i = 0; i < EZ_ARRAY_SIZE(m_hCubemapProxies); ++i)
  {
    m_hCubemapProxies[i] = ezGALDevice::GetDefaultDevice()->CreateProxyTexture(m_hCubemap, i);

    sName.Format("Reflection Cubemap Proxy {}", i);
    pDevice->GetTexture(m_hCubemapProxies[i])->SetDebugName(sName);
  }
}

ProbeUpdateInfo::~ProbeUpdateInfo()
{
  for (ezUInt32 i = 0; i < EZ_ARRAY_SIZE(m_hCubemapProxies); ++i)
  {
    if (!m_hCubemapProxies[i].IsInvalidated())
    {
      ezGALDevice::GetDefaultDevice()->DestroyProxyTexture(m_hCubemapProxies[i]);
    }
  }

  if (!m_hCubemap.IsInvalidated())
  {
    ezGPUResourcePool::GetDefaultInstance()->ReturnRenderTarget(m_hCubemap);
  }
}

void ProbeUpdateInfo::Reset()
{
  m_bInUse = false;
  m_UpdateTarget.m_Id.Invalidate();
  m_UpdateTarget.m_uiWorldIndex = 0;
  m_LastUpdateStep = UpdateStep::Default;
  m_UpdateSteps.Clear();
}

void ProbeUpdateInfo::Setup(DynamicUpdate target)
{
  m_UpdateTarget = target;
  m_bInUse = true;
  m_Mode = ezReflectionProbeMode::Dynamic;
  //#TODO other stuff.
}

//////////////////////////////////////////////////////////////////////////
/// ezReflectionPool::Data

ezReflectionPool::Data::Data()
{
  m_SkyIrradianceStorage.SetCount(64);
}

ezReflectionPool::Data::~Data()
{
  for (auto& renderView : m_RenderViews)
  {
    ezRenderWorld::DeleteView(renderView.m_hView);
  }

  for (auto& filterView : m_FilterViews)
  {
    ezRenderWorld::DeleteView(filterView.m_hView);
  }

  if (!m_hFallbackReflectionSpecularTexture.IsInvalidated())
  {
    ezGALDevice::GetDefaultDevice()->DestroyTexture(m_hFallbackReflectionSpecularTexture);
    m_hFallbackReflectionSpecularTexture.Invalidate();
  }

  ezUInt32 uiWorldReflectionCount = m_WorldReflectionData.GetCount();
  for (ezUInt32 i = 0; i < uiWorldReflectionCount; ++i)
  {
    WorldReflectionData* pData = m_WorldReflectionData[i].Borrow();
    EZ_ASSERT_DEV(!pData || pData->m_uiRegisteredProbeCount == 0, "Not all probes were deregistered.");
  }
  m_WorldReflectionData.Clear();

  if (!m_hSkyIrradianceTexture.IsInvalidated())
  {
    ezGALDevice::GetDefaultDevice()->DestroyTexture(m_hSkyIrradianceTexture);
    m_hSkyIrradianceTexture.Invalidate();
  }
}

//void ezReflectionPool::Data::SortActiveProbes(ezUInt64 uiFrameCounter)
//{
//  m_SortedUpdateInfo.Clear();
//
//  for (ezUInt32 uiActiveProbeIndex = 0; uiActiveProbeIndex < m_ActiveDynamicProbes.GetCount(); ++uiActiveProbeIndex)
//  {
//    ProbeUpdateInfo* pUpdateInfo = nullptr;
//    if (m_UpdateInfo.TryGetValue(m_ActiveDynamicProbes[uiActiveProbeIndex], pUpdateInfo))
//    {
//      auto& sorted = m_SortedUpdateInfo.ExpandAndGetRef();
//      sorted.m_pUpdateInfo = pUpdateInfo;
//      sorted.m_uiIndex = uiActiveProbeIndex;
//      sorted.m_fPriority = pUpdateInfo->GetPriority(uiFrameCounter);
//    }
//  }
//
//  m_SortedUpdateInfo.Sort();
//
//  for (ezUInt32 i = 0; i < m_SortedUpdateInfo.GetCount(); ++i)
//  {
//    m_SortedUpdateInfo[i].m_pUpdateInfo->m_uiIndexInUpdateQueue = i;
//  }
//}

void ezReflectionPool::Data::GenerateUpdateSteps()
{
  ezUInt32 uiRenderViewIndex = 0;
  ezUInt32 uiFilterViewIndex = 0;

  ezUInt32 uiSortedUpdateInfoIndex = 0;
  while (uiSortedUpdateInfoIndex < m_DynamicUpdates.GetCount())
  {
    auto pUpdateInfo = m_DynamicUpdates[uiSortedUpdateInfoIndex].Borrow();
    if (!pUpdateInfo->m_bInUse)
    {
      ++uiSortedUpdateInfoIndex;
      continue;
    }

    auto& updateSteps = pUpdateInfo->m_UpdateSteps;
    UpdateStep::Enum nextStep = UpdateStep::NextStep(updateSteps.IsEmpty() ? pUpdateInfo->m_LastUpdateStep : updateSteps.PeekBack().m_UpdateStep);
    if (pUpdateInfo->m_Mode == ezReflectionProbeMode::Static)
      nextStep = UpdateStep::Filter;

    bool bNextProbe = false;

    if (UpdateStep::IsRenderStep(nextStep))
    {
      if (uiRenderViewIndex < m_RenderViews.GetCount())
      {
        updateSteps.PushBack({(ezUInt8)uiRenderViewIndex, nextStep});
        ++uiRenderViewIndex;
      }
      else
      {
        bNextProbe = true;
      }
    }
    else if (nextStep == UpdateStep::Filter)
    {
      // don't do render and filter in one frame
      if (uiFilterViewIndex < m_FilterViews.GetCount() && updateSteps.IsEmpty())
      {
        updateSteps.PushBack({(ezUInt8)uiFilterViewIndex, nextStep});
        ++uiFilterViewIndex;
      }
      bNextProbe = true;
    }

    // break if no more views are available
    if (uiRenderViewIndex == m_RenderViews.GetCount() && uiFilterViewIndex == m_FilterViews.GetCount())
    {
      break;
    }

    //// advance to next probe if it has the same priority as the current probe
    //if (uiSortedUpdateInfoIndex + 1 < s_pData->m_SortedUpdateInfo.GetCount())
    //{
    //  bNextProbe |= s_pData->m_SortedUpdateInfo[uiSortedUpdateInfoIndex + 1].m_fPriority == sortedUpdateInfo.m_fPriority;
    //}

    if (bNextProbe)
    {
      ++uiSortedUpdateInfoIndex;
    }
  }
}

void ezReflectionPool::Data::ResetProbeUpdateInfo(ProbeUpdateInfo* pInfo)
{
  // Reset and move to the end of the queue.
  pInfo->Reset();
  const ezUInt32 uiCount = m_DynamicUpdates.GetCount();
  for (ezUInt32 i = 0; i < uiCount; i++)
  {
    if (m_DynamicUpdates[i] == pInfo)
    {
      ezUniquePtr<ProbeUpdateInfo> info = std::move(m_DynamicUpdates[i]);
      m_DynamicUpdates.RemoveAtAndCopy(i);
      m_DynamicUpdates.PushBack(std::move(info));
      return;
    }
  }
}

void ezReflectionPool::Data::CreateWorldReflectionData(ezReflectionPool::Data::WorldReflectionData& data)
{
  EZ_ASSERT_DEV(data.m_hReflectionSpecularTexture.IsInvalidated(), "World data already created.");
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();
  ezGALTextureCreationDescription desc;
  desc.m_uiWidth = s_uiReflectionCubeMapSize;
  desc.m_uiHeight = s_uiReflectionCubeMapSize;
  desc.m_uiMipLevelCount = GetMipLevels();
  desc.m_uiArraySize = s_uiNumReflectionProbeCubeMaps;
  desc.m_Format = ezGALResourceFormat::RGBAHalf;
  desc.m_Type = ezGALTextureType::TextureCube;
  desc.m_bCreateRenderTarget = true;
  desc.m_ResourceAccess.m_bReadBack = true;
  desc.m_ResourceAccess.m_bImmutable = false;

  data.m_hReflectionSpecularTexture = pDevice->CreateTexture(desc);
  pDevice->GetTexture(data.m_hReflectionSpecularTexture)->SetDebugName("Reflection Specular Texture");
}

void ezReflectionPool::Data::DestroyWorldReflectionData(ezReflectionPool::Data::WorldReflectionData& data)
{
  EZ_ASSERT_DEV(!data.m_hReflectionSpecularTexture.IsInvalidated(), "World data not created.");
  ezGALDevice::GetDefaultDevice()->DestroyTexture(data.m_hReflectionSpecularTexture);
  data.m_hReflectionSpecularTexture.Invalidate();
}

static void CreateViews(
  ezDynamicArray<ReflectionView>& views, ezUInt32 uiMaxRenderViews, const char* szNameSuffix, const char* szRenderPipelineResource)
{
  uiMaxRenderViews = ezMath::Max<ezUInt32>(uiMaxRenderViews, 1);

  if (uiMaxRenderViews > views.GetCount())
  {
    ezStringBuilder sName;

    ezUInt32 uiCurrentCount = views.GetCount();
    for (ezUInt32 i = uiCurrentCount; i < uiMaxRenderViews; ++i)
    {
      auto& renderView = views.ExpandAndGetRef();

      sName.Format("Reflection Probe {} {}", szNameSuffix, i);

      ezView* pView = nullptr;
      renderView.m_hView = ezRenderWorld::CreateView(sName, pView);

      pView->SetCameraUsageHint(ezCameraUsageHint::Reflection);
      pView->SetViewport(ezRectFloat(0.0f, 0.0f, static_cast<float>(s_uiReflectionCubeMapSize), static_cast<float>(s_uiReflectionCubeMapSize)));

      pView->SetRenderPipelineResource(ezResourceManager::LoadResource<ezRenderPipelineResource>(szRenderPipelineResource));

      renderView.m_Camera.SetCameraMode(ezCameraMode::PerspectiveFixedFovX, 90.0f, 0.1f, 100.0f); // TODO: expose
      pView->SetCamera(&renderView.m_Camera);
    }
  }
  else if (uiMaxRenderViews < views.GetCount())
  {
    views.SetCount(uiMaxRenderViews);
  }
}

void ezReflectionPool::Data::CreateReflectionViewsAndResources()
{
  // ReflectionRenderPipeline.ezRenderPipelineAsset
  CreateViews(m_RenderViews, cvar_RenderingReflectionPoolMaxRenderViews, "Render", "{ 734898e8-b1a2-0da2-c4ae-701912983c2f }");

  // ReflectionFilterPipeline.ezRenderPipelineAsset
  CreateViews(m_FilterViews, cvar_RenderingReflectionPoolMaxFilterViews, "Filter", "{ 3437db17-ddf1-4b67-b80f-9999d6b0c352 }");

  if (m_DynamicUpdates.IsEmpty())
  {
    for (ezUInt32 i = 0; i < 2; i++)
    {
      m_DynamicUpdates.PushBack(EZ_DEFAULT_NEW(ProbeUpdateInfo));
    }
  }

  if (m_hFallbackReflectionSpecularTexture.IsInvalidated())
  {
    ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

    ezGALTextureCreationDescription desc;
    desc.m_uiWidth = s_uiReflectionCubeMapSize;
    desc.m_uiHeight = s_uiReflectionCubeMapSize;
    desc.m_uiMipLevelCount = GetMipLevels();
    desc.m_uiArraySize = 1;
    desc.m_Format = ezGALResourceFormat::RGBAHalf;
    desc.m_Type = ezGALTextureType::TextureCube;
    desc.m_bCreateRenderTarget = true;
    desc.m_ResourceAccess.m_bReadBack = true;
    desc.m_ResourceAccess.m_bImmutable = false;

    m_hFallbackReflectionSpecularTexture = pDevice->CreateTexture(desc);
    pDevice->GetTexture(m_hFallbackReflectionSpecularTexture)->SetDebugName("Reflection Fallback Specular Texture");
  }

#if EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
  if (!m_hDebugSphere.IsValid())
  {
    ezGeometry geom;
    geom.AddSphere(s_fDebugSphereRadius, 32, 16, ezColor::White);

    const char* szBufferResourceName = "ReflectionProbeDebugSphereBuffer";
    ezMeshBufferResourceHandle hMeshBuffer = ezResourceManager::GetExistingResource<ezMeshBufferResource>(szBufferResourceName);
    if (!hMeshBuffer.IsValid())
    {
      ezMeshBufferResourceDescriptor desc;
      desc.AddStream(ezGALVertexAttributeSemantic::Position, ezGALResourceFormat::XYZFloat);
      desc.AddStream(ezGALVertexAttributeSemantic::Normal, ezGALResourceFormat::XYZFloat);
      desc.AllocateStreamsFromGeometry(geom, ezGALPrimitiveTopology::Triangles);

      hMeshBuffer = ezResourceManager::CreateResource<ezMeshBufferResource>(szBufferResourceName, std::move(desc), szBufferResourceName);
    }

    const char* szMeshResourceName = "ReflectionProbeDebugSphere";
    m_hDebugSphere = ezResourceManager::GetExistingResource<ezMeshResource>(szMeshResourceName);
    if (!m_hDebugSphere.IsValid())
    {
      ezMeshResourceDescriptor desc;
      desc.UseExistingMeshBuffer(hMeshBuffer);
      desc.AddSubMesh(geom.CalculateTriangleCount(), 0, 0);
      desc.ComputeBounds();

      m_hDebugSphere = ezResourceManager::CreateResource<ezMeshResource>(szMeshResourceName, std::move(desc), szMeshResourceName);
    }
  }

  if (m_hDebugMaterial.IsEmpty())
  {
    const ezUInt32 uiMipLevelCount = GetMipLevels();

    ezMaterialResourceHandle hDebugMaterial = ezResourceManager::LoadResource<ezMaterialResource>(
      "{ 6f8067d0-ece8-44e1-af46-79b49266de41 }"); // ReflectionProbeVisualization.ezMaterialAsset
    ezResourceLock<ezMaterialResource> pMaterial(hDebugMaterial, ezResourceAcquireMode::BlockTillLoaded);
    if (pMaterial->GetLoadingState() != ezResourceState::Loaded)
      return;

    ezMaterialResourceDescriptor desc = pMaterial->GetCurrentDesc();
    ezUInt32 uiMipLevel = desc.m_Parameters.GetCount();
    ezUInt32 uiReflectionProbeIndex = desc.m_Parameters.GetCount();
    ezTempHashedString sMipLevelParam = "MipLevel";
    ezTempHashedString sReflectionProbeIndexParam = "ReflectionProbeIndex";
    for (ezUInt32 i = 0; i < desc.m_Parameters.GetCount(); ++i)
    {
      if (desc.m_Parameters[i].m_Name == sMipLevelParam)
      {
        uiMipLevel = i;
      }
      if (desc.m_Parameters[i].m_Name == sReflectionProbeIndexParam)
      {
        uiReflectionProbeIndex = i;
      }
    }

    if (uiMipLevel >= desc.m_Parameters.GetCount() || uiReflectionProbeIndex >= desc.m_Parameters.GetCount())
      return;

    m_hDebugMaterial.SetCount(uiMipLevelCount * s_uiNumReflectionProbeCubeMaps);
    for (ezUInt32 iReflectionProbeIndex = 0; iReflectionProbeIndex < s_uiNumReflectionProbeCubeMaps; iReflectionProbeIndex++)
    {
      for (ezUInt32 iMipLevel = 0; iMipLevel < uiMipLevelCount; iMipLevel++)
      {
        desc.m_Parameters[uiMipLevel].m_Value = iMipLevel;
        desc.m_Parameters[uiReflectionProbeIndex].m_Value = iReflectionProbeIndex;
        ezStringBuilder sMaterialName;
        sMaterialName.Format("ReflectionProbeVisualization - MipLevel {}, Index {}", iMipLevel, iReflectionProbeIndex);

        ezMaterialResourceDescriptor desc2 = desc;
        m_hDebugMaterial[iReflectionProbeIndex * uiMipLevelCount + iMipLevel] = ezResourceManager::CreateResource<ezMaterialResource>(sMaterialName, std::move(desc2));
      }
    }
  }
#endif
}

void ezReflectionPool::Data::CreateSkyIrradianceTexture()
{
  if (m_hSkyIrradianceTexture.IsInvalidated())
  {
    ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

    ezGALTextureCreationDescription desc;
    desc.m_uiWidth = 6;
    desc.m_uiHeight = 64;
    desc.m_Format = ezGALResourceFormat::RGBAHalf;
    desc.m_Type = ezGALTextureType::Texture2D;
    desc.m_bCreateRenderTarget = true;
    desc.m_bAllowUAV = true;

    m_hSkyIrradianceTexture = pDevice->CreateTexture(desc);
    pDevice->GetTexture(m_hSkyIrradianceTexture)->SetDebugName("Sky Irradiance Texture");
  }
}

void ezReflectionPool::Data::AddViewToRender(const ProbeUpdateInfo::Step& step, const ProbeData& probeData, ProbeUpdateInfo& updateInfo)
{
  ezVec3 vForward[6] = {
    ezVec3(1.0f, 0.0f, 0.0f),
    ezVec3(-1.0f, 0.0f, 0.0f),
    ezVec3(0.0f, 0.0f, 1.0f),
    ezVec3(0.0f, 0.0f, -1.0f),
    ezVec3(0.0f, -1.0f, 0.0f),
    ezVec3(0.0f, 1.0f, 0.0f),
  };

  ezVec3 vUp[6] = {
    ezVec3(0.0f, 0.0f, 1.0f),
    ezVec3(0.0f, 0.0f, 1.0f),
    ezVec3(0.0f, 1.0f, 0.0f),
    ezVec3(0.0f, -1.0f, 0.0f),
    ezVec3(0.0f, 0.0f, 1.0f),
    ezVec3(0.0f, 0.0f, 1.0f),
  };

  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  // Setup view and camera
  {
    ReflectionView* pReflectionView = nullptr;
    ezUInt32 uiFaceIndex = 0;

    if (step.m_UpdateStep == UpdateStep::Filter)
    {
      pReflectionView = &m_FilterViews[step.m_uiViewIndex];
    }
    else
    {
      pReflectionView = &m_RenderViews[step.m_uiViewIndex];
      uiFaceIndex = step.m_UpdateStep;
    }

    ezView* pView = nullptr;
    ezRenderWorld::TryGetView(pReflectionView->m_hView, pView);

    pView->m_IncludeTags = probeData.m_desc.m_IncludeTags;
    pView->m_ExcludeTags = probeData.m_desc.m_ExcludeTags;
    ezWorld* pWorld = ezWorld::GetWorld(updateInfo.m_UpdateTarget.m_uiWorldIndex);
    pView->SetWorld(pWorld);

    ezGALRenderTargetSetup renderTargetSetup;
    if (step.m_UpdateStep == UpdateStep::Filter)
    {
      const ezUInt32 uiWorldIndex = pWorld->GetIndex();
      renderTargetSetup.SetRenderTarget(0, pDevice->GetDefaultRenderTargetView(m_WorldReflectionData[uiWorldIndex]->m_hReflectionSpecularTexture));

      ezUInt32 bla = 2;
      if (probeData.m_Flags.IsSet(ezProbeFlags::SkyLight))
      {
        bla = pWorld->GetIndex();
        renderTargetSetup.SetRenderTarget(2, pDevice->GetDefaultRenderTargetView(m_hSkyIrradianceTexture));
      }
      pView->SetRenderPassProperty("ReflectionFilterPass", "Intensity", probeData.m_desc.m_fIntensity);
      pView->SetRenderPassProperty("ReflectionFilterPass", "Saturation", probeData.m_desc.m_fSaturation);
      pView->SetRenderPassProperty("ReflectionFilterPass", "SpecularOutputIndex", probeData.m_uiReflectionIndex);
      pView->SetRenderPassProperty("ReflectionFilterPass", "IrradianceOutputIndex", bla);
      ezGALTextureHandle hSourceTexture = updateInfo.m_hCubemap;
      if (probeData.m_desc.m_Mode == ezReflectionProbeMode::Static)
      {
        ezResourceLock<ezTextureCubeResource> pTexture(probeData.m_hCubeMap, ezResourceAcquireMode::BlockTillLoaded);
        //#TODO Currently even in static mode we render the 6 sides and only change the filter stage to point to the static texture if available. Rendering the 6 sides is intended only in the editor as a preview for non-baked probes. We will need to find a way to quickly determine if we need to do this fallback at a much earlier stage.
        if (pTexture->GetLoadingState() == ezResourceState::Loaded && pTexture->GetResourceHandle() == probeData.m_hCubeMap)
        {
          hSourceTexture = pTexture->GetGALTexture();
        }
      }
      pView->SetRenderPassProperty("ReflectionFilterPass", "InputCubemap", hSourceTexture.GetInternalID().m_Data);
    }
    else
    {
      renderTargetSetup.SetRenderTarget(0, pDevice->GetDefaultRenderTargetView(updateInfo.m_hCubemapProxies[uiFaceIndex]));
    }
    pView->SetRenderTargetSetup(renderTargetSetup);

    ezVec3 vPosition = probeData.m_GlobalTransform * probeData.m_desc.m_vCaptureOffset;
    ezVec3 vForward2 = probeData.m_GlobalTransform.TransformDirection(vForward[uiFaceIndex]);
    ezVec3 vUp2 = probeData.m_GlobalTransform.TransformDirection(vUp[uiFaceIndex]);
    if (probeData.m_Flags.IsSet(ezProbeFlags::SkyLight))
    {
      vForward2 = vForward[uiFaceIndex];
      vUp2 = vUp[uiFaceIndex];
    }

    const float fFar = probeData.m_desc.m_fFarPlane;
    float fNear = probeData.m_desc.m_fNearPlane;
    if (fNear >= fFar)
    {
      fNear = fFar - 0.001f;
    }
    else if (fNear == 0.0f)
    {
      fNear = fFar / 1000.0f;
    }

    pReflectionView->m_Camera.LookAt(vPosition, vPosition + vForward2, vUp2);
    pReflectionView->m_Camera.SetCameraMode(ezCameraMode::PerspectiveFixedFovX, 90.0f, fNear, fFar);
    ezRenderWorld::AddViewToRender(pReflectionView->m_hView);
  }
}

ezReflectionPool::Data* ezReflectionPool::s_pData;

//////////////////////////////////////////////////////////////////////////
/// ezReflectionPool


//
//// static
//void ezReflectionPool::ExtractReflectionProbe(
//  ezMsgExtractRenderData& msg, const ezReflectionProbeData& data, const ezComponent* pComponent, bool& bStatesDirty, float fPriority /*= 1.0f*/)
//{
//  fPriority = ezMath::Clamp(fPriority, ezMath::DefaultEpsilon<float>(), 100.0f);
//  ezVec3 vPosition = pComponent->GetOwner()->GetGlobalPosition();
//
//  ProbeUpdateInfo* pUpdateInfo = nullptr;
//  if (!s_pData->m_UpdateInfo.TryGetValue(data.m_Id, pUpdateInfo))
//    return;
//
//  ezUInt64 uiCurrentFrame = ezRenderWorld::GetFrameCounter();
//  bool bExecuteUpdateSteps = false;
//
//  {
//    EZ_LOCK(s_pData->m_Mutex);
//
//    pUpdateInfo->m_fPriority = ezMath::Max(pUpdateInfo->m_fPriority, fPriority);
//
//    bool bFirstTimeCalledThisFrame = false;
//    if (pUpdateInfo->m_uiLastActiveFrame != uiCurrentFrame)
//    {
//      pUpdateInfo->m_uiLastActiveFrame = uiCurrentFrame;
//      bExecuteUpdateSteps = !pUpdateInfo->m_UpdateSteps.IsEmpty();
//      // This function can be called multiple times per frame (once per view) but execution is only run once.
//      bFirstTimeCalledThisFrame = true;
//    }
//
//    if (pUpdateInfo->m_Mode != data.m_Mode)
//    {
//      // If the mode changes we restart the cycle by setting the last update step to filter, which is the last step in the cycle.
//      pUpdateInfo->m_Mode = data.m_Mode;
//      // Clear current step and don't execute it as it probably no longer makes sense to run it.
//      bExecuteUpdateSteps = false;
//      pUpdateInfo->m_LastUpdateStep = UpdateStep::Filter;
//      pUpdateInfo->m_UpdateSteps.Clear();
//    }
//
//    if (bFirstTimeCalledThisFrame)
//    {
//      // Add as active for next frame if states have changed and we are not
//      if (pUpdateInfo->m_Mode == ezReflectionProbeMode::Dynamic || bStatesDirty && !bExecuteUpdateSteps)
//        s_pData->m_ActiveDynamicProbes.PushBack(data.m_Id);
//
//      if (pUpdateInfo->m_Mode == ezReflectionProbeMode::Static)
//      {
//        // Wait until the input texture is fully loaded.
//        ezResourceLock<ezTextureCubeResource> pTexture(data.m_hCubeMap, ezResourceAcquireMode::AllowLoadingFallback);
//        if (pTexture->GetLoadingState() != ezResourceState::Loaded || pTexture->GetNumQualityLevelsLoadable() > 0 || pTexture->GetResourceHandle() != data.m_hCubeMap)
//        {
//          bExecuteUpdateSteps = false;
//        }
//      }
//    }
//  }
//
//  if (bExecuteUpdateSteps)
//  {
//    bool bFiltered = false;
//    for (ezUInt32 i = pUpdateInfo->m_UpdateSteps.GetCount(); i-- > 0;)
//    {
//      if (pUpdateInfo->m_UpdateSteps[i].m_UpdateStep == UpdateStep::Filter)
//        bStatesDirty = false;
//      s_pData->AddViewToRender(pUpdateInfo->m_UpdateSteps[i], data, *pUpdateInfo, vPosition);
//    }
//
//    pUpdateInfo->m_LastUpdateStep = pUpdateInfo->m_UpdateSteps.PeekBack().m_UpdateStep;
//    pUpdateInfo->m_UpdateSteps.Clear();
//
//    pUpdateInfo->m_uiLastUpdatedFrame = uiCurrentFrame;
//  }
//
//
//}

// static
void ezReflectionPool::SetConstantSkyIrradiance(const ezWorld* pWorld, const ezAmbientCube<ezColor>& skyIrradiance)
{
  ezUInt32 uiWorldIndex = pWorld->GetIndex();
  ezAmbientCube<ezColorLinear16f> skyIrradiance16f = skyIrradiance;

  auto& skyIrradianceStorage = s_pData->m_SkyIrradianceStorage;
  if (skyIrradianceStorage[uiWorldIndex] != skyIrradiance16f)
  {
    skyIrradianceStorage[uiWorldIndex] = skyIrradiance16f;

    s_pData->m_uiSkyIrradianceChanged |= EZ_BIT(uiWorldIndex);
  }
}

void ezReflectionPool::ResetConstantSkyIrradiance(const ezWorld* pWorld)
{
  ezUInt32 uiWorldIndex = pWorld->GetIndex();

  auto& skyIrradianceStorage = s_pData->m_SkyIrradianceStorage;
  if (skyIrradianceStorage[uiWorldIndex] != ezAmbientCube<ezColorLinear16f>())
  {
    skyIrradianceStorage[uiWorldIndex] = ezAmbientCube<ezColorLinear16f>();

    s_pData->m_uiSkyIrradianceChanged |= EZ_BIT(uiWorldIndex);
  }
}

// static
ezUInt32 ezReflectionPool::GetReflectionCubeMapSize()
{
  return s_uiReflectionCubeMapSize;
}

// static
ezGALTextureHandle ezReflectionPool::GetReflectionSpecularTexture(ezUInt32 uiWorldIndex)
{
  if (uiWorldIndex < s_pData->m_WorldReflectionData.GetCount())
  {
    Data::WorldReflectionData* pData = s_pData->m_WorldReflectionData[uiWorldIndex].Borrow();
    if (pData)
      return pData->m_hReflectionSpecularTexture;
  }
  return s_pData->m_hFallbackReflectionSpecularTexture;
}

// static
ezGALTextureHandle ezReflectionPool::GetSkyIrradianceTexture()
{
  return s_pData->m_hSkyIrradianceTexture;
}

// static
void ezReflectionPool::OnEngineStartup()
{
  s_pData = EZ_DEFAULT_NEW(ezReflectionPool::Data);

  ezRenderWorld::GetExtractionEvent().AddEventHandler(OnExtractionEvent);
  ezRenderWorld::GetRenderEvent().AddEventHandler(OnRenderEvent);
}

// static
void ezReflectionPool::OnEngineShutdown()
{
  ezRenderWorld::GetExtractionEvent().RemoveEventHandler(OnExtractionEvent);
  ezRenderWorld::GetRenderEvent().RemoveEventHandler(OnRenderEvent);

  EZ_DEFAULT_DELETE(s_pData);
}

// static
void ezReflectionPool::OnExtractionEvent(const ezRenderWorldExtractionEvent& e)
{
  if (e.m_Type == ezRenderWorldExtractionEvent::Type::BeginExtraction)
  {
    EZ_PROFILE_SCOPE("Reflection Pool BeginExtraction");
    s_pData->CreateSkyIrradianceTexture();
    s_pData->CreateReflectionViewsAndResources();
    s_pData->PreExtraction();
    s_pData->GenerateUpdateSteps();
  }

  if (e.m_Type == ezRenderWorldExtractionEvent::Type::EndExtraction)
  {
    EZ_PROFILE_SCOPE("Reflection Pool EndExtraction");
    s_pData->PostExtraction();
  }
}

// static
void ezReflectionPool::OnRenderEvent(const ezRenderWorldRenderEvent& e)
{
  if (e.m_Type != ezRenderWorldRenderEvent::Type::BeginRender)
    return;

  if (s_pData->m_hSkyIrradianceTexture.IsInvalidated())
    return;

  EZ_LOCK(s_pData->m_Mutex);

  ezUInt64 uiWorldHasSkyLight = s_pData->m_uiWorldHasSkyLight;
  ezUInt64 uiSkyIrradianceChanged = s_pData->m_uiSkyIrradianceChanged;
  if ((~uiWorldHasSkyLight & uiSkyIrradianceChanged) == 0)
    return;

  auto& skyIrradianceStorage = s_pData->m_SkyIrradianceStorage;

  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  auto pGALPass = pDevice->BeginPass("Sky Irradiance Texture Update");
  auto pGALCommandEncoder = pGALPass->BeginCompute();
  EZ_SCOPE_EXIT(pGALPass->EndCompute(pGALCommandEncoder); pDevice->EndPass(pGALPass););

  for (ezUInt32 i = 0; i < skyIrradianceStorage.GetCount(); ++i)
  {
    if ((uiWorldHasSkyLight & EZ_BIT(i)) == 0 && (uiSkyIrradianceChanged & EZ_BIT(i)) != 0)
    {
      ezBoundingBoxu32 destBox;
      destBox.m_vMin.Set(0, i, 0);
      destBox.m_vMax.Set(6, i + 1, 1);

      ezGALSystemMemoryDescription memDesc;
      memDesc.m_pData = &skyIrradianceStorage[i].m_Values[0];
      memDesc.m_uiRowPitch = sizeof(ezAmbientCube<ezColorLinear16f>);

      pGALCommandEncoder->UpdateTexture(s_pData->m_hSkyIrradianceTexture, ezGALTextureSubresource(), destBox, memDesc);

      uiSkyIrradianceChanged &= ~EZ_BIT(i);

      // TODO: Clear reflection cubemap
    }
  }
}


EZ_STATICLINK_FILE(RendererCore, RendererCore_Lights_Implementation_ReflectionPool);
