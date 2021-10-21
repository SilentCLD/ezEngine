#include <RendererCore/RendererCorePCH.h>

#include <Core/Graphics/Camera.h>
#include <Core/Graphics/Geometry.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Configuration/Startup.h>
#include <Foundation/Math/Color16f.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/GPUResourcePool/GPUResourcePool.h>
#include <RendererCore/Lights/Implementation/ReflectionPool.h>
#include <RendererCore/Lights/Implementation/ReflectionPoolDeclarations.h>
#include <RendererCore/Lights/Implementation/ReflectionProbeData.h>
#include <RendererCore/Meshes/MeshComponentBase.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererCore/Textures/TextureCubeResource.h>
#include <RendererFoundation/CommandEncoder/ComputeCommandEncoder.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Device/Pass.h>
#include <RendererFoundation/Profiling/Profiling.h>
#include <RendererFoundation/Resources/Texture.h>

// clang-format off
EZ_BEGIN_STATIC_REFLECTED_BITFLAGS(ezProbeFlags, 1)
  EZ_BITFLAGS_CONSTANTS(ezProbeFlags::SkyLight, ezProbeFlags::HasCustomCubeMap, ezProbeFlags::Sphere, ezProbeFlags::Box, ezProbeFlags::Dirty, ezProbeFlags::Dynamic, ezProbeFlags::Usable)
EZ_END_STATIC_REFLECTED_BITFLAGS;
// clang-format on


//////////////////////////////////////////////////////////////////////////
/// ezReflectionPool::Data

ezReflectionProbeId ezReflectionPool::Data::AddProbe(const ezWorld* pWorld, ProbeData&& probeData)
{
  const ezUInt32 uiWorldIndex = pWorld->GetIndex();

  if (uiWorldIndex >= s_pData->m_WorldReflectionData.GetCount())
    s_pData->m_WorldReflectionData.SetCount(uiWorldIndex + 1);

  ezReflectionPool::Data::WorldReflectionData& worldReflectionData = s_pData->m_WorldReflectionData[uiWorldIndex];
  if (worldReflectionData.m_uiRegisteredProbeCount == 0)
  {
    s_pData->CreateWorldReflectionData(worldReflectionData);
  }
  worldReflectionData.m_uiRegisteredProbeCount++;

  const bool bIsSkyLight = probeData.m_Flags.IsSet(ezProbeFlags::SkyLight);
  ezReflectionProbeId id = worldReflectionData.m_Probes.Insert(std::move(probeData));
  if (bIsSkyLight)
  {
    worldReflectionData.m_SkyLight = id;
  }
  return id;
}

ezReflectionPool::Data::WorldReflectionData& ezReflectionPool::Data::GetWorldData(const ezWorld* pWorld)
{
  const ezUInt32 uiWorldIndex = pWorld->GetIndex();
  return s_pData->m_WorldReflectionData[uiWorldIndex];
}

void ezReflectionPool::Data::RemoveProbe(const ezWorld* pWorld, ezReflectionProbeId id)
{
  const ezUInt32 uiWorldIndex = pWorld->GetIndex();
  ezReflectionPool::Data::WorldReflectionData& data = s_pData->m_WorldReflectionData[uiWorldIndex];

  UnmapProbe(uiWorldIndex, data, id);

  data.m_uiRegisteredProbeCount--;
  if (data.m_SkyLight == id)
  {
    data.m_SkyLight.Invalidate();
  }

  data.m_Probes.Remove(id);

  if (data.m_uiRegisteredProbeCount == 0)
  {
    s_pData->DestroyWorldReflectionData(data);
  }
}

bool ezReflectionPool::Data::UpdateProbeData(ProbeData& probeData, const ezReflectionProbeDesc& desc, const ezReflectionProbeComponent* pComponent)
{
  bool bProbeTypeChanged = false;
  if (probeData.m_uiReflectionIndex >= 0)
  {
    if (probeData.m_desc.m_Mode != desc.m_Mode)
    {
      //#TODO any other reason to unmap a probe.
      bProbeTypeChanged = true;
    }
  }

  probeData.m_desc = desc;
  probeData.m_GlobalTransform = pComponent->GetOwner()->GetGlobalTransform();

  if (const ezSphereReflectionProbeComponent* pSphere = ezDynamicCast<const ezSphereReflectionProbeComponent*>(pComponent))
  {
    probeData.m_Flags = ezProbeFlags::Sphere;
    probeData.m_vHalfExtents = ezVec3(pSphere->GetRadius());
  }
  else if (const ezBoxReflectionProbeComponent* pBox = ezDynamicCast<const ezBoxReflectionProbeComponent*>(pComponent))
  {
    probeData.m_Flags = ezProbeFlags::Box;
    probeData.m_vHalfExtents = pBox->GetExtents() / 2.0f;
  }

  if (probeData.m_desc.m_Mode == ezReflectionProbeMode::Dynamic)
  {
    probeData.m_Flags |= (ezProbeFlags::Dynamic | ezProbeFlags::Dirty);
  }
  else
  {
    ezStringBuilder sComponentGuid, sCubeMapFile;
    ezConversionUtils::ToString(probeData.m_desc.m_uniqueID, sComponentGuid);

    // this is where the editor will put the file for this probe
    sCubeMapFile.Format(":project/AssetCache/Generated/{0}.ezTexture", sComponentGuid);

    probeData.m_hCubeMap = ezResourceManager::LoadResource<ezTextureCubeResource>(sCubeMapFile);
  }
  return bProbeTypeChanged;
}

bool ezReflectionPool::Data::UpdateSkyLightData(ProbeData& probeData, const ezReflectionProbeDesc& desc, const ezSkyLightComponent* pComponent)
{
  bool bProbeTypeChanged = false;
  if (probeData.m_uiReflectionIndex >= 0)
  {
    if (probeData.m_desc.m_Mode != desc.m_Mode)
    {
      //#TODO any other reason to unmap a probe.
      bProbeTypeChanged = true;
    }
  }

  probeData.m_desc = desc;
  probeData.m_GlobalTransform = pComponent->GetOwner()->GetGlobalTransform();

  if (auto pSkyLight = ezDynamicCast<const ezSkyLightComponent*>(pComponent))
  {
    probeData.m_Flags = ezProbeFlags::SkyLight;
    probeData.m_hCubeMap = pSkyLight->GetCubeMap();
    if (probeData.m_desc.m_Mode == ezReflectionProbeMode::Dynamic)
    {
      probeData.m_Flags |= (ezProbeFlags::Dynamic | ezProbeFlags::Dirty);
    }
    else
    {
      if (probeData.m_hCubeMap.IsValid())
      {
        probeData.m_Flags |= ezProbeFlags::HasCustomCubeMap;
      }
      else
      {
        ezStringBuilder sComponentGuid, sCubeMapFile;
        ezConversionUtils::ToString(probeData.m_desc.m_uniqueID, sComponentGuid);

        // this is where the editor will put the file for this probe
        sCubeMapFile.Format(":project/AssetCache/Generated/{0}.ezTexture", sComponentGuid);

        probeData.m_hCubeMap = ezResourceManager::LoadResource<ezTextureCubeResource>(sCubeMapFile);
      }
    }
    probeData.m_vHalfExtents = ezVec3::ZeroVector();
  }
  return bProbeTypeChanged;
}

void ezReflectionPool::Data::MapProbe(const ezUInt32 uiWorldIndex, ezReflectionPool::Data::WorldReflectionData& data, ezReflectionProbeId id, ezInt32 iReflectionIndex)
{
  ProbeData& probeData = data.m_Probes.GetValueUnchecked(id.m_InstanceIndex);
  probeData.m_uiReflectionIndex = iReflectionIndex;
  EZ_ASSERT_DEBUG(data.m_MappedCubes[probeData.m_uiReflectionIndex].IsInvalidated(), "A probe is already mapped on this index.");
  data.m_MappedCubes[probeData.m_uiReflectionIndex] = id;
  data.m_ActiveProbes.PushBack({id, 0.0f});
}

void ezReflectionPool::Data::UnmapProbe(const ezUInt32 uiWorldIndex, ezReflectionPool::Data::WorldReflectionData& data, ezReflectionProbeId id)
{
  ProbeData& probeData = data.m_Probes.GetValueUnchecked(id.m_InstanceIndex);
  if (probeData.m_uiReflectionIndex != -1)
  {
    //data.m_UnusedProbeSlots.PushBack(probeData.m_uiReflectionIndex);
    data.m_MappedCubes[probeData.m_uiReflectionIndex].Invalidate();
    probeData.m_uiReflectionIndex = -1;

    DynamicUpdate probeUpdate = {uiWorldIndex, id};
    if (m_PendingDynamicUpdate.Contains(probeUpdate))
    {
      m_PendingDynamicUpdate.Remove(probeUpdate);
      m_DynamicUpdateQueue.RemoveAndCopy(probeUpdate);
    }

    if (probeData.m_uiIndexInUpdateQueue != -1)
    {
      m_DynamicUpdates[probeData.m_uiIndexInUpdateQueue].Reset();
      probeData.m_uiIndexInUpdateQueue = -1;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
/// Probes

ezReflectionProbeId ezReflectionPool::RegisterReflectionProbe(const ezWorld* pWorld, const ezReflectionProbeDesc& desc, const ezReflectionProbeComponent* pComponent)
{
  EZ_LOCK(s_pData->m_Mutex);

  ProbeData probe;
  s_pData->UpdateProbeData(probe, desc, pComponent);

  return s_pData->AddProbe(pWorld, std::move(probe));
}

void ezReflectionPool::DeregisterReflectionProbe(const ezWorld* pWorld, ezReflectionProbeId id)
{
  EZ_LOCK(s_pData->m_Mutex);
  s_pData->RemoveProbe(pWorld, id);
}

void ezReflectionPool::UpdateReflectionProbe(const ezWorld* pWorld, ezReflectionProbeId id, const ezReflectionProbeDesc& desc, const ezReflectionProbeComponent* pComponent)
{
  EZ_LOCK(s_pData->m_Mutex);
  ezReflectionPool::Data::WorldReflectionData& data = s_pData->GetWorldData(pWorld);
  ProbeData& probeData = data.m_Probes.GetValueUnchecked(id.m_InstanceIndex);
  if (s_pData->UpdateProbeData(probeData, desc, pComponent))
  {
    s_pData->UnmapProbe(pWorld->GetIndex(), data, id);
  }
}

void ezReflectionPool::ExtractReflectionProbe(const ezComponent* pComponent, ezMsgExtractRenderData& msg, ezReflectionProbeRenderData* pRenderData, const ezWorld* pWorld, ezReflectionProbeId id, float fPriority)
{
  EZ_LOCK(s_pData->m_Mutex);
  const ezUInt32 uiWorldIndex = pWorld->GetIndex();
  ezReflectionPool::Data::WorldReflectionData& data = s_pData->m_WorldReflectionData[uiWorldIndex];
  ProbeData& probeData = data.m_Probes.GetValueUnchecked(id.m_InstanceIndex);
  probeData.m_fPriority += fPriority;
  const ezInt32 iMappedIndex = probeData.m_uiReflectionIndex;

  ezStringBuilder sEnum;
  ezReflectionUtils::EnumerationToString(probeData.m_Flags, sEnum, ezReflectionUtils::EnumConversionMode::ValueNameOnly);
  ezStringBuilder s;
  s.Format("\n RefIdx: {}\nUpdateInx: {}\nFlags: {}\n", iMappedIndex, probeData.m_uiIndexInUpdateQueue, sEnum);
  ezDebugRenderer::Draw3DText(pWorld, s, pComponent->GetOwner()->GetGlobalPosition(), ezColor::Violet);

  if (probeData.m_uiIndexInUpdateQueue != -1)
  {
    ProbeUpdateInfo& info = s_pData->m_DynamicUpdates[probeData.m_uiIndexInUpdateQueue];
    EZ_ASSERT_DEBUG(info.m_UpdateTarget.m_Id == id, "");
    if (!info.m_bCalledThisFrame)
    {
      bool bDone = false;
      info.m_bCalledThisFrame = true;
      if (!info.m_UpdateSteps.IsEmpty())
      {
        for (ezUInt32 i = info.m_UpdateSteps.GetCount(); i-- > 0;)
        {
          if (info.m_UpdateSteps[i].m_UpdateStep == UpdateStep::Filter)
          {
            bDone = true;
            //bStatesDirty = false;
          }

          ezTransform transform = pComponent->GetOwner()->GetGlobalTransform();
          ProbeData& probeData = data.m_Probes.GetValueUnchecked(id.m_InstanceIndex);
          s_pData->AddViewToRender(info.m_UpdateSteps[i], probeData, info, transform);
        }

        info.m_LastUpdateStep = info.m_UpdateSteps.PeekBack().m_UpdateStep;
        info.m_UpdateSteps.Clear();
        if (bDone)
        {
          info.Reset();
          probeData.m_uiIndexInUpdateQueue = -1;
          probeData.m_Flags |= ezProbeFlags::Usable;
        }
      }
    }
  }


  // The sky light is always active and not added to the render data (always passes in nullptr as pRenderData).
  if (iMappedIndex > 0 && pRenderData)
  {
    // Index and flags are stored in m_uiIndex so we can't just overwrite it.
    pRenderData->m_uiIndex |= (ezUInt32)iMappedIndex;
    msg.AddRenderData(pRenderData, ezDefaultRenderDataCategories::ReflectionProbe, ezRenderData::Caching::Never);
  }

  // Not mapped in the atlas - cannot render it.
  if (iMappedIndex < 0)
    return;

#if EZ_ENABLED(EZ_COMPILE_FOR_DEVELOPMENT)
  const ezUInt32 uiMipLevels = GetMipLevels();
  if (probeData.m_desc.m_bShowDebugInfo && s_pData->m_hDebugMaterial.GetCount() == uiMipLevels * s_uiNumReflectionProbeCubeMaps)
  {
    const ezGameObject* pOwner = pComponent->GetOwner();
    const ezTransform ownerTransform = pOwner->GetGlobalTransform();
    for (ezUInt32 i = 0; i < uiMipLevels; i++)
    {
      ezMeshRenderData* pRenderData = ezCreateRenderDataForThisFrame<ezMeshRenderData>(pOwner);
      pRenderData->m_GlobalTransform.m_vPosition = ownerTransform.m_vPosition;
      pRenderData->m_GlobalTransform.m_vScale = ezVec3(1.0f);
      if (!probeData.m_Flags.IsSet(ezProbeFlags::SkyLight))
      {
        pRenderData->m_GlobalTransform.m_qRotation = ownerTransform.m_qRotation;
      }
      pRenderData->m_GlobalTransform.m_vPosition.z += s_fDebugSphereRadius * i * 2;
      pRenderData->m_GlobalBounds = pOwner->GetGlobalBounds();
      pRenderData->m_hMesh = s_pData->m_hDebugSphere;
      pRenderData->m_hMaterial = s_pData->m_hDebugMaterial[iMappedIndex * uiMipLevels + i];
      pRenderData->m_Color = ezColor::White;
      pRenderData->m_uiSubMeshIndex = 0;
      pRenderData->m_uiUniqueID = ezRenderComponent::GetUniqueIdForRendering(pComponent, 0);

      pRenderData->FillBatchIdAndSortingKey();
      msg.AddRenderData(pRenderData, ezDefaultRenderDataCategories::LitOpaque, ezRenderData::Caching::IfStatic);
    }

    if (msg.m_OverrideCategory == ezInvalidRenderDataCategory)
    {
      /*ezStringBuilder sTemp;
      sTemp.Format("Priority: {}\\nIndex in Update Queue: {}", pUpdateInfo->m_fPriority, pUpdateInfo->m_uiIndexInUpdateQueue);

      ezDebugRenderer::Draw3DText(msg.m_pView->GetHandle(), sTemp, vPosition + ezVec3(0, 0, s_fDebugSphereRadius), ezColor::LightPink);*/
    }
  }
#endif
}

//////////////////////////////////////////////////////////////////////////
/// SkyLight

ezReflectionProbeId ezReflectionPool::RegisterSkyLight(const ezWorld* pWorld, ezReflectionProbeDesc& desc, const ezSkyLightComponent* pComponent)
{
  EZ_LOCK(s_pData->m_Mutex);
  const ezUInt32 uiWorldIndex = pWorld->GetIndex();
  s_pData->m_uiWorldHasSkyLight |= EZ_BIT(uiWorldIndex);
  s_pData->m_uiSkyIrradianceChanged |= EZ_BIT(uiWorldIndex);

  ProbeData probe;
  s_pData->UpdateSkyLightData(probe, desc, pComponent);

  ezReflectionProbeId id = s_pData->AddProbe(pWorld, std::move(probe));

  ezReflectionPool::Data::WorldReflectionData& worldReflectionData = s_pData->m_WorldReflectionData[uiWorldIndex];

  // Always map sky light at slot 0.
  s_pData->MapProbe(uiWorldIndex, worldReflectionData, id, 0);

  return id;
}

void ezReflectionPool::DeregisterSkyLight(const ezWorld* pWorld, ezReflectionProbeId id)
{
  EZ_LOCK(s_pData->m_Mutex);

  s_pData->RemoveProbe(pWorld, id);

  const ezUInt32 uiWorldIndex = pWorld->GetIndex();
  s_pData->m_uiWorldHasSkyLight &= ~EZ_BIT(uiWorldIndex);
  s_pData->m_uiSkyIrradianceChanged |= EZ_BIT(uiWorldIndex);
}

void ezReflectionPool::UpdateSkyLight(const ezWorld* pWorld, ezReflectionProbeId id, const ezReflectionProbeDesc& desc, const ezSkyLightComponent* pComponent)
{
  EZ_LOCK(s_pData->m_Mutex);
  ezReflectionPool::Data::WorldReflectionData& data = s_pData->GetWorldData(pWorld);
  ProbeData& probeData = data.m_Probes.GetValueUnchecked(id.m_InstanceIndex);
  if (s_pData->UpdateSkyLightData(probeData, desc, pComponent))
  {
    s_pData->UnmapProbe(pWorld->GetIndex(), data, id);
  }
}

//////////////////////////////////////////////////////////////////////////
/// Dynamic Update

void ezReflectionPool::Data::PreExtraction()
{
  EZ_LOCK(s_pData->m_Mutex);
  const ezUInt32 uiWorldCount = s_pData->m_WorldReflectionData.GetCount();

  for (ezUInt32 uiWorld = 0; uiWorld < uiWorldCount; uiWorld++)
  {
    ezReflectionPool::Data::WorldReflectionData& data = s_pData->m_WorldReflectionData[uiWorld];
    if (data.m_uiRegisteredProbeCount == 0)
      continue;

    // Reset priorities
    for (auto it = data.m_Probes.GetIterator(); it.IsValid(); ++it)
    {
      it.Value().m_fPriority = 0.0f;
    }
    if (!data.m_SkyLight.IsInvalidated())
    {
      data.m_Probes.GetValueUnchecked(data.m_SkyLight.m_InstanceIndex).m_fPriority = ezMath::MaxValue<float>();
    }

    data.m_SortedProbes.Clear();
    data.m_ActiveProbes.Clear();
    data.m_UnusedProbeSlots.Clear();
    data.m_AddProbes.Clear();
  }

  
  // Schedule new dynamic updates
  {
    const ezUInt32 uiCount = m_DynamicUpdates.GetCount();
    for (ezUInt32 i = 0; i < uiCount; i++)
    {
      ProbeUpdateInfo& info = m_DynamicUpdates[i];
      info.m_bCalledThisFrame = false;
      // is done?
      if (!info.m_bInUse && !m_DynamicUpdateQueue.IsEmpty())
      {
        DynamicUpdate nextUpdate = m_DynamicUpdateQueue.PeekFront();
        m_DynamicUpdateQueue.PopFront();
        m_PendingDynamicUpdate.Remove(nextUpdate);
        info.Setup(nextUpdate);

        ezReflectionPool::Data::WorldReflectionData& data = s_pData->m_WorldReflectionData[nextUpdate.m_uiWorldIndex];
        ProbeData& probeData = data.m_Probes.GetValueUnchecked(nextUpdate.m_Id.m_InstanceIndex);
        probeData.m_uiIndexInUpdateQueue = i;
      }
    }
  }
}

void ezReflectionPool::Data::PostExtraction()
{
  EZ_LOCK(s_pData->m_Mutex);
  const ezUInt32 uiWorldCount = s_pData->m_WorldReflectionData.GetCount();
  for (ezUInt32 uiWorld = 0; uiWorld < uiWorldCount; uiWorld++)
  {
    ezReflectionPool::Data::WorldReflectionData& data = s_pData->m_WorldReflectionData[uiWorld];
    if (data.m_uiRegisteredProbeCount == 0)
      continue;

    {
      // Sort all active non-skylight probes so we can find the best candidates to evict from the atlas.
      for (ezUInt32 i = 1; i < s_uiNumReflectionProbeCubeMaps; i++)
      {
        auto id = data.m_MappedCubes[i];
        if (!id.IsInvalidated())
        {
          data.m_ActiveProbes.PushBack({id, data.m_Probes.GetValueUnchecked(id.m_InstanceIndex).m_fPriority});
        }
        else
        {
          data.m_UnusedProbeSlots.PushBack(i);
        }
      }
      data.m_ActiveProbes.Sort();
    }

    {
      // Sort all exiting probes by priority.
      data.m_SortedProbes.Reserve(data.m_Probes.GetCount());
      for (auto it = data.m_Probes.GetIterator(); it.IsValid(); ++it)
      {
        data.m_SortedProbes.PushBack({it.Id(), it.Value().m_fPriority});
      }
      data.m_SortedProbes.Sort();
    }

    {
      // Look at the first N best probes that would ideally be mapped in the atlas and find unmapped ones.
      const ezUInt32 uiMaxCount = ezMath::Min(s_uiNumReflectionProbeCubeMaps, data.m_SortedProbes.GetCount());
      for (ezUInt32 i = 0; i < uiMaxCount; i++)
      {
        const SortedProbes& probe = data.m_SortedProbes[i];
        ProbeData& probeData = data.m_Probes.GetValueUnchecked(probe.m_uiIndex.m_InstanceIndex);

        if (probeData.m_uiReflectionIndex < 0)
        {
          // We found a better probe to be mapped to the atlas.
          data.m_AddProbes.PushBack(probe);
        }
      }
    }

    {
      // Trigger resource loading of static or updates of dynamic probes.
      const ezUInt32 uiMaxCount = data.m_AddProbes.GetCount();
      for (ezUInt32 i = 0; i < uiMaxCount; i++)
      {
        const SortedProbes& probe = data.m_AddProbes[i];
        ProbeData& probeData = data.m_Probes.GetValueUnchecked(probe.m_uiIndex.m_InstanceIndex);
        //#TODO static probe resource loading
      }
    }

    // Unmap probes in case we need free slots using results from last frame
    {
      // Only unmap one probe per frame
      //#TODO better heuristic to decide how many if any should be unmapped.
      if (data.m_UnusedProbeSlots.GetCount() == 0 && data.m_AddProbes.GetCount() > 0)
      {
        const SortedProbes probe = data.m_ActiveProbes.PeekBack();
        UnmapProbe(uiWorld, data, probe.m_uiIndex);
      }
    }

    // Map probes with higher priority
    {
      const ezUInt32 uiMaxCount = ezMath::Min(data.m_AddProbes.GetCount(), data.m_UnusedProbeSlots.GetCount());
      for (ezUInt32 i = 0; i < uiMaxCount; i++)
      {
        ezInt32 iReflectionIndex = data.m_UnusedProbeSlots[i];
        const SortedProbes probe = data.m_AddProbes[i];
        MapProbe(uiWorld, data, probe.m_uiIndex, iReflectionIndex);
      }
    }

    // Enqueue dynamic probe updates
    {
      // We add the skylight again as we want to consider it for dynamic updates.
      if (!data.m_SkyLight.IsInvalidated())
      {
        data.m_ActiveProbes.PushBack({data.m_SkyLight, ezMath::MaxValue<float>()});
      }
      const ezUInt32 uiMaxCount = data.m_ActiveProbes.GetCount();
      for (ezUInt32 i = 0; i < uiMaxCount; i++)
      {
        const SortedProbes probe = data.m_ActiveProbes[i];
        const ProbeData& probeData = data.m_Probes.GetValueUnchecked(probe.m_uiIndex.m_InstanceIndex);
        if (probeData.m_Flags.IsSet(ezProbeFlags::Dynamic))
        {
          // For now, we just manage a FIFO queue of all dynamic probes that have a high enough priority.
          const DynamicUpdate du = {uiWorld, probe.m_uiIndex};
          if (probeData.m_uiIndexInUpdateQueue == -1 && !m_PendingDynamicUpdate.Contains(du))
          {
            m_PendingDynamicUpdate.Insert(du);
            m_DynamicUpdateQueue.PushBack(du);
          }
        }
        else
        {
          //#TODO Add static probes once resources are loaded.
        }
      }
    }
  }

}
