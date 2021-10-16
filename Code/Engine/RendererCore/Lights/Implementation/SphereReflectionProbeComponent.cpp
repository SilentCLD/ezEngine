#include <RendererCore/RendererCorePCH.h>

#include <Core/Messages/UpdateLocalBoundsMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <RendererCore/Lights/Implementation/ReflectionPool.h>
#include <RendererCore/Lights/SphereReflectionProbeComponent.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererCore/Textures/TextureCubeResource.h>
#include <../../Data/Base/Shaders/Common/LightData.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>

namespace
{
  static ezVariantArray GetDefaultTags()
  {
    ezVariantArray value(ezStaticAllocatorWrapper::GetAllocator());
    value.PushBack(ezStringView("CastShadows"));
    return value;
  }
} // namespace

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezReflectionProbeRenderData, 1, ezRTTIDefaultAllocator<ezReflectionProbeRenderData>)
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezReflectionProbeComponent, 2, ezRTTINoAllocator)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ENUM_ACCESSOR_PROPERTY("ReflectionProbeMode", ezReflectionProbeMode, GetReflectionProbeMode, SetReflectionProbeMode)->AddAttributes(new ezDefaultValueAttribute(ezReflectionProbeMode::Static), new ezGroupAttribute("Description")),
    EZ_ACCESSOR_PROPERTY("Intensity", GetIntensity, SetIntensity)->AddAttributes(new ezClampValueAttribute(0.0f, ezVariant()), new ezDefaultValueAttribute(1.0f)),
    EZ_ACCESSOR_PROPERTY("Saturation", GetSaturation, SetSaturation)->AddAttributes(new ezClampValueAttribute(0.0f, ezVariant()), new ezDefaultValueAttribute(1.0f)),
    EZ_SET_ACCESSOR_PROPERTY("IncludeTags", GetIncludeTags, InsertIncludeTag, RemoveIncludeTag)->AddAttributes(new ezTagSetWidgetAttribute("Default"), new ezDefaultValueAttribute(GetDefaultTags())),
    EZ_SET_ACCESSOR_PROPERTY("ExcludeTags", GetExcludeTags, InsertExcludeTag, RemoveExcludeTag)->AddAttributes(new ezTagSetWidgetAttribute("Default")),
    EZ_ACCESSOR_PROPERTY("ShowDebugInfo", GetShowDebugInfo, SetShowDebugInfo),
  }
  EZ_END_PROPERTIES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_COMPONENT_TYPE(ezSphereReflectionProbeComponent, 1, ezComponentMode::Dynamic)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Radius", GetRadius, SetRadius)->AddAttributes(new ezClampValueAttribute(0.0f, {}), new ezDefaultValueAttribute(5.0f)),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_FUNCTIONS
  {
    EZ_FUNCTION_PROPERTY(OnObjectCreated),
  }
  EZ_END_FUNCTIONS;
  EZ_BEGIN_MESSAGEHANDLERS
  {
    EZ_MESSAGE_HANDLER(ezMsgUpdateLocalBounds, OnUpdateLocalBounds),
    EZ_MESSAGE_HANDLER(ezMsgExtractRenderData, OnMsgExtractRenderData),
  }
  EZ_END_MESSAGEHANDLERS;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezCategoryAttribute("Rendering/Lighting"),
    new ezSphereVisualizerAttribute("Radius", ezColor::AliceBlue),
    new ezSphereManipulatorAttribute("Radius"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_COMPONENT_TYPE


EZ_BEGIN_COMPONENT_TYPE(ezBoxReflectionProbeComponent, 1, ezComponentMode::Dynamic)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Extents", GetExtents, SetExtents)->AddAttributes(new ezClampValueAttribute(ezVec3(0.0f), {}), new ezDefaultValueAttribute(ezVec3(5.0f))),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_FUNCTIONS
  {
    EZ_FUNCTION_PROPERTY(OnObjectCreated),
  }
  EZ_END_FUNCTIONS;
  EZ_BEGIN_MESSAGEHANDLERS
  {
    EZ_MESSAGE_HANDLER(ezMsgUpdateLocalBounds, OnUpdateLocalBounds),
    EZ_MESSAGE_HANDLER(ezMsgExtractRenderData, OnMsgExtractRenderData),
  }
  EZ_END_MESSAGEHANDLERS;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezCategoryAttribute("Rendering/Lighting"),
    new ezBoxVisualizerAttribute("Extents", ezColor::AliceBlue),
    new ezBoxManipulatorAttribute("Extents"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_COMPONENT_TYPE
// clang-format on

ezSphereReflectionProbeComponentManager::ezSphereReflectionProbeComponentManager(ezWorld* pWorld)
  : ezComponentManager<ezSphereReflectionProbeComponent, ezBlockStorageType::Compact>(pWorld)
{
}

void ezSphereReflectionProbeComponentManager::Initialize()
{
}

ezBoxReflectionProbeComponentManager::ezBoxReflectionProbeComponentManager(ezWorld* pWorld)
  : ezComponentManager<ezBoxReflectionProbeComponent, ezBlockStorageType::Compact>(pWorld)
{
}

void ezBoxReflectionProbeComponentManager::Initialize()
{
}

//////////////////////////////////////////////////////////////////////////

ezReflectionProbeComponent::ezReflectionProbeComponent()
{
  m_desc.m_uniqueID.CreateNewUuid();
}

ezReflectionProbeComponent::~ezReflectionProbeComponent()
{
}

void ezReflectionProbeComponent::SetReflectionProbeMode(ezEnum<ezReflectionProbeMode> mode)
{
  m_desc.m_Mode = mode;
  m_bStatesDirty = true;
}

ezEnum<ezReflectionProbeMode> ezReflectionProbeComponent::GetReflectionProbeMode() const
{
  return m_desc.m_Mode;
}

void ezReflectionProbeComponent::SetIntensity(float fIntensity)
{
  m_desc.m_fIntensity = fIntensity;
  m_bStatesDirty = true;
}

float ezReflectionProbeComponent::GetIntensity() const
{
  return m_desc.m_fIntensity;
}

void ezReflectionProbeComponent::SetSaturation(float fSaturation)
{
  m_desc.m_fSaturation = fSaturation;
  m_bStatesDirty = true;
}

float ezReflectionProbeComponent::GetSaturation() const
{
  return m_desc.m_fSaturation;
}

const ezTagSet& ezReflectionProbeComponent::GetIncludeTags() const
{
  return m_desc.m_IncludeTags;
}

void ezReflectionProbeComponent::InsertIncludeTag(const char* szTag)
{
  m_desc.m_IncludeTags.SetByName(szTag);
  m_bStatesDirty = true;
}

void ezReflectionProbeComponent::RemoveIncludeTag(const char* szTag)
{
  m_desc.m_IncludeTags.RemoveByName(szTag);
  m_bStatesDirty = true;
}


const ezTagSet& ezReflectionProbeComponent::GetExcludeTags() const
{
  return m_desc.m_ExcludeTags;
}

void ezReflectionProbeComponent::InsertExcludeTag(const char* szTag)
{
  m_desc.m_ExcludeTags.SetByName(szTag);
  m_bStatesDirty = true;
}

void ezReflectionProbeComponent::RemoveExcludeTag(const char* szTag)
{
  m_desc.m_ExcludeTags.RemoveByName(szTag);
  m_bStatesDirty = true;
}

void ezReflectionProbeComponent::SetShowDebugInfo(bool bShowDebugInfo)
{
  m_desc.m_bShowDebugInfo = bShowDebugInfo;
  m_bStatesDirty = true;
}

bool ezReflectionProbeComponent::GetShowDebugInfo() const
{
  return m_desc.m_bShowDebugInfo;
}

void ezReflectionProbeComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);

  ezStreamWriter& s = stream.GetStream();

  m_desc.m_IncludeTags.Save(s);
  m_desc.m_ExcludeTags.Save(s);
  s << m_desc.m_Mode;
  s << m_desc.m_bShowDebugInfo;
  s << m_desc.m_fIntensity;
  s << m_desc.m_fSaturation;
  s << m_desc.m_uniqueID;
}

void ezReflectionProbeComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  //const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  ezStreamReader& s = stream.GetStream();

  m_desc.m_IncludeTags.Load(s, ezTagRegistry::GetGlobalRegistry());
  m_desc.m_ExcludeTags.Load(s, ezTagRegistry::GetGlobalRegistry());
  s >> m_desc.m_Mode;
  s >> m_desc.m_bShowDebugInfo;
  s >> m_desc.m_fIntensity;
  s >> m_desc.m_fSaturation;
  s >> m_desc.m_uniqueID;
}

//////////////////////////////////////////////////////////////////////////

ezSphereReflectionProbeComponent::ezSphereReflectionProbeComponent() = default;
ezSphereReflectionProbeComponent::~ezSphereReflectionProbeComponent() = default;

void ezSphereReflectionProbeComponent::SetRadius(float fRadius)
{
  m_fRadius = ezMath::Max(fRadius, 0.0f);
  m_bStatesDirty = true;
}

float ezSphereReflectionProbeComponent::GetRadius() const
{
  return m_fRadius;
}

void ezSphereReflectionProbeComponent::OnActivated()
{
  m_Id = ezReflectionPool::RegisterReflectionProbe(GetWorld(), m_desc, this);
  GetOwner()->UpdateLocalBounds();
}

void ezSphereReflectionProbeComponent::OnDeactivated()
{
  ezReflectionPool::DeregisterReflectionProbe(GetWorld(), m_Id);
  m_Id.Invalidate();

  GetOwner()->UpdateLocalBounds();
}

void ezSphereReflectionProbeComponent::OnObjectCreated(const ezAbstractObjectNode& node)
{
  m_desc.m_uniqueID = node.GetGuid();
}

void ezSphereReflectionProbeComponent::OnUpdateLocalBounds(ezMsgUpdateLocalBounds& msg)
{
  msg.SetAlwaysVisible(GetOwner()->IsDynamic() ? ezDefaultSpatialDataCategories::RenderDynamic : ezDefaultSpatialDataCategories::RenderStatic);
}

void ezSphereReflectionProbeComponent::OnMsgExtractRenderData(ezMsgExtractRenderData& msg) const
{
  // Don't trigger reflection rendering in shadow or other reflection views.
  if (msg.m_pView->GetCameraUsageHint() == ezCameraUsageHint::Shadow || msg.m_pView->GetCameraUsageHint() == ezCameraUsageHint::Reflection)
    return;

  if (m_bStatesDirty)
  {
    m_bStatesDirty = false;
    ezReflectionPool::UpdateReflectionProbe(GetWorld(), m_Id, m_desc, this);
  }

  auto pRenderData = ezCreateRenderDataForThisFrame<ezReflectionProbeRenderData>(GetOwner());
  pRenderData->m_GlobalTransform = GetOwner()->GetGlobalTransform();
  const ezVec3 vScale = pRenderData->m_GlobalTransform.m_vScale * m_fRadius;
  const float fVolume = (4.0f / 3.0f) * ezMath::Pi<float>() * ezMath::Abs(vScale.x * vScale.y * vScale.z);
  const float fLogVolume = ezMath::Log2(1.0f + fVolume); // +1 to make sure it never goes negative.
  // This sorting is only by size to make sure the probes in a cluster are iterating from smallest to largest on the GPU. Which probes are actually used is determined below by the GetReflectionProbeIndex call.
  pRenderData->m_uiSortingKey = ezMath::FloatToInt(ezMath::MaxValue<ezUInt32>() * fLogVolume / 40.0f);

  //
  float fPriority = 0.0f;
  if (msg.m_pView)
  {
    if (auto pCamera = msg.m_pView->GetLodCamera())
    {
      float fDistance = (pCamera->GetPosition() - pRenderData->m_GlobalTransform.m_vPosition).GetLength();
      float fRadius = (ezMath::Abs(vScale.x) + ezMath::Abs(vScale.y) + ezMath::Abs(vScale.z)) / 3.0f;
      fPriority = fRadius / fDistance;
    }
  }
  ezStringBuilder s;
  s.Format("{}, {}", pRenderData->m_uiSortingKey, fPriority);
  ezDebugRenderer::Draw3DText(GetWorld(), s, pRenderData->m_GlobalTransform.m_vPosition, ezColor::Wheat);

  pRenderData->m_vHalfExtents = ezVec3(m_fRadius);
  pRenderData->m_Id = m_Id;
  pRenderData->m_uiIndex = REFLECTION_PROBE_IS_SPHERE;
  ezReflectionPool::ExtractReflectionProbe(this, msg, pRenderData, GetWorld(), m_Id, fPriority); 
}

void ezSphereReflectionProbeComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);

  ezStreamWriter& s = stream.GetStream();

  s << m_fRadius;
}

void ezSphereReflectionProbeComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  //const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  ezStreamReader& s = stream.GetStream();

  s >> m_fRadius;
}

//////////////////////////////////////////////////////////////////////////

ezBoxReflectionProbeComponent::ezBoxReflectionProbeComponent() = default;
ezBoxReflectionProbeComponent::~ezBoxReflectionProbeComponent() = default;

void ezBoxReflectionProbeComponent::SetExtents(const ezVec3& extents)
{
  m_vExtents = extents;
  m_bStatesDirty = true;
}

const ezVec3& ezBoxReflectionProbeComponent::GetExtents() const
{
  return m_vExtents;
}

void ezBoxReflectionProbeComponent::OnActivated()
{
  m_Id = ezReflectionPool::RegisterReflectionProbe(GetWorld(), m_desc, this);
  GetOwner()->UpdateLocalBounds();
}

void ezBoxReflectionProbeComponent::OnDeactivated()
{
  ezReflectionPool::DeregisterReflectionProbe(GetWorld(), m_Id);
  m_Id.Invalidate();

  GetOwner()->UpdateLocalBounds();
}

void ezBoxReflectionProbeComponent::OnObjectCreated(const ezAbstractObjectNode& node)
{
  m_desc.m_uniqueID = node.GetGuid();
}

void ezBoxReflectionProbeComponent::OnUpdateLocalBounds(ezMsgUpdateLocalBounds& msg)
{
  msg.SetAlwaysVisible(GetOwner()->IsDynamic() ? ezDefaultSpatialDataCategories::RenderDynamic : ezDefaultSpatialDataCategories::RenderStatic);
}

void ezBoxReflectionProbeComponent::OnMsgExtractRenderData(ezMsgExtractRenderData& msg) const
{
  // Don't trigger reflection rendering in shadow or other reflection views.
  if (msg.m_pView->GetCameraUsageHint() == ezCameraUsageHint::Shadow || msg.m_pView->GetCameraUsageHint() == ezCameraUsageHint::Reflection)
    return;

  if (m_bStatesDirty)
  {
    m_bStatesDirty = false;
    ezReflectionPool::UpdateReflectionProbe(GetWorld(), m_Id, m_desc, this);
  }

  auto pRenderData = ezCreateRenderDataForThisFrame<ezReflectionProbeRenderData>(GetOwner());
  pRenderData->m_GlobalTransform = GetOwner()->GetGlobalTransform();
  const ezVec3 vScale = pRenderData->m_GlobalTransform.m_vScale.CompMul(m_vExtents);
  const float fVolume = ezMath::Abs(vScale.x * vScale.y * vScale.z);
  const float fLogVolume = ezMath::Log2(1.0f + fVolume); // +1 to make sure it never goes negative.
  // This sorting is only by size to make sure the probes in a cluster are iterating from smallest to largest on the GPU. Which probes are actually used is determined below by the GetReflectionProbeIndex call.
  pRenderData->m_uiSortingKey = ezMath::FloatToInt(ezMath::MaxValue<ezUInt32>() * fLogVolume / 40.0f);

  //
  float fPriority = 0.0f;
  if (msg.m_pView)
  {
    if (auto pCamera = msg.m_pView->GetLodCamera())
    {
      float fDistance = (pCamera->GetPosition() - pRenderData->m_GlobalTransform.m_vPosition).GetLength();
      float fRadius = (ezMath::Abs(vScale.x) + ezMath::Abs(vScale.y) + ezMath::Abs(vScale.z)) / 3.0f;
      fPriority = fRadius / fDistance;
    }
  }
  ezStringBuilder s;
  s.Format("{}, {}", pRenderData->m_uiSortingKey, fPriority);
  ezDebugRenderer::Draw3DText(GetWorld(), s, pRenderData->m_GlobalTransform.m_vPosition, ezColor::Wheat);

  pRenderData->m_vHalfExtents = m_vExtents / 2.0f;
  pRenderData->m_Id = m_Id;
  pRenderData->m_uiIndex = 0;
  ezReflectionPool::ExtractReflectionProbe(this, msg, pRenderData, GetWorld(), m_Id, fPriority);
}

void ezBoxReflectionProbeComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);

  ezStreamWriter& s = stream.GetStream();

  s << m_vExtents;
}

void ezBoxReflectionProbeComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  //const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  ezStreamReader& s = stream.GetStream();

  s >> m_vExtents;
}

EZ_STATICLINK_FILE(RendererCore, RendererCore_Lights_Implementation_SphereReflectionProbeComponent);
