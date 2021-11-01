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
#include <Core/Messages/TransformChangedMessage.h>

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezReflectionProbeRenderData, 1, ezRTTIDefaultAllocator<ezReflectionProbeRenderData>)
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezReflectionProbeComponent, 2, ezRTTINoAllocator)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ENUM_ACCESSOR_PROPERTY("ReflectionProbeMode", ezReflectionProbeMode, GetReflectionProbeMode, SetReflectionProbeMode)->AddAttributes(new ezDefaultValueAttribute(ezReflectionProbeMode::Static), new ezGroupAttribute("Description")),
    EZ_SET_ACCESSOR_PROPERTY("IncludeTags", GetIncludeTags, InsertIncludeTag, RemoveIncludeTag)->AddAttributes(new ezTagSetWidgetAttribute("Default")),
    EZ_SET_ACCESSOR_PROPERTY("ExcludeTags", GetExcludeTags, InsertExcludeTag, RemoveExcludeTag)->AddAttributes(new ezTagSetWidgetAttribute("Default")),
    EZ_ACCESSOR_PROPERTY("NearPlane", GetNearPlane, SetNearPlane)->AddAttributes(new ezDefaultValueAttribute(0.0f), new ezClampValueAttribute(0.0f, {}), new ezMinValueTextAttribute("Auto")),
    EZ_ACCESSOR_PROPERTY("FarPlane", GetFarPlane, SetFarPlane)->AddAttributes(new ezDefaultValueAttribute(100.0f), new ezClampValueAttribute(0.01f, 10000.0f)),
    EZ_ACCESSOR_PROPERTY("CaptureOffset", GetCaptureOffset, SetCaptureOffset),
    EZ_ACCESSOR_PROPERTY("ShowDebugInfo", GetShowDebugInfo, SetShowDebugInfo),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezTransformManipulatorAttribute("CaptureOffset"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_COMPONENT_TYPE(ezSphereReflectionProbeComponent, 1, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Radius", GetRadius, SetRadius)->AddAttributes(new ezClampValueAttribute(0.0f, {}), new ezDefaultValueAttribute(5.0f)),
    EZ_ACCESSOR_PROPERTY("Falloff", GetFalloff, SetFalloff)->AddAttributes(new ezClampValueAttribute(0.0f, 1.0f), new ezDefaultValueAttribute(0.1f)),
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
    EZ_MESSAGE_HANDLER(ezMsgTransformChanged, OnTransformChanged),
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


EZ_BEGIN_COMPONENT_TYPE(ezBoxReflectionProbeComponent, 1, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Extents", GetExtents, SetExtents)->AddAttributes(new ezClampValueAttribute(ezVec3(0.0f), {}), new ezDefaultValueAttribute(ezVec3(5.0f))),
    EZ_ACCESSOR_PROPERTY("InfluenceScale", GetInfluenceScale, SetInfluenceScale)->AddAttributes(new ezClampValueAttribute(ezVec3(0.0f), ezVec3(1.0f)), new ezDefaultValueAttribute(ezVec3(1.0f))),
    EZ_ACCESSOR_PROPERTY("InfluenceShift", GetInfluenceShift, SetInfluenceShift)->AddAttributes(new ezClampValueAttribute(ezVec3(-1.0f), ezVec3(1.0f)), new ezDefaultValueAttribute(ezVec3(0.0f))),
    EZ_ACCESSOR_PROPERTY("PositiveFalloff", GetPositiveFalloff, SetPositiveFalloff)->AddAttributes(new ezClampValueAttribute(ezVec3(0.0f), ezVec3(1.0f)), new ezDefaultValueAttribute(ezVec3(0.1f))),
    EZ_ACCESSOR_PROPERTY("NegativeFalloff", GetNegativeFalloff, SetNegativeFalloff)->AddAttributes(new ezClampValueAttribute(ezVec3(0.0f), ezVec3(1.0f)), new ezDefaultValueAttribute(ezVec3(0.1f))),
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
    EZ_MESSAGE_HANDLER(ezMsgTransformChanged, OnTransformChanged),
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

void ezReflectionProbeComponent::SetNearPlane(float fNearPlane)
{
  m_desc.m_fNearPlane = fNearPlane;
  m_bStatesDirty = true;
}

void ezReflectionProbeComponent::SetFarPlane(float fFarPlane)
{
  m_desc.m_fFarPlane = fFarPlane;
  m_bStatesDirty = true;
}

void ezReflectionProbeComponent::SetCaptureOffset(const ezVec3& vOffset)
{
  m_desc.m_vCaptureOffset = vOffset;
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
  s << m_desc.m_uniqueID;
  s << m_desc.m_fNearPlane;
  s << m_desc.m_fFarPlane;
  s << m_desc.m_vCaptureOffset;
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
  s >> m_desc.m_uniqueID;
  s >> m_desc.m_fNearPlane;
  s >> m_desc.m_fFarPlane;
  s >> m_desc.m_vCaptureOffset;
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

void ezSphereReflectionProbeComponent::SetFalloff(float fFalloff)
{
  m_fFalloff = ezMath::Clamp(fFalloff, ezMath::DefaultEpsilon<float>(), 1.0f);
}

void ezSphereReflectionProbeComponent::OnActivated()
{
  GetOwner()->EnableStaticTransformChangesNotifications();
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
  msg.SetAlwaysVisible(ezDefaultSpatialDataCategories::RenderDynamic);
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
  pRenderData->m_vProbePosition = pRenderData->m_GlobalTransform * m_desc.m_vCaptureOffset;
  pRenderData->m_vInfluenceScale = ezVec3(1.0f);
  pRenderData->m_vInfluenceShift = ezVec3(0.5f);
  pRenderData->m_vPositiveFalloff = ezVec3(m_fFalloff);
  pRenderData->m_vNegativeFalloff = ezVec3(m_fFalloff);
  pRenderData->m_Id = m_Id;
  pRenderData->m_uiIndex = REFLECTION_PROBE_IS_SPHERE;
  ezReflectionPool::ExtractReflectionProbe(this, msg, pRenderData, GetWorld(), m_Id, fPriority); 
}

void ezSphereReflectionProbeComponent::OnTransformChanged(ezMsgTransformChanged& msg)
{
  m_bStatesDirty = true;
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
}

const ezVec3& ezBoxReflectionProbeComponent::GetInfluenceScale() const
{
  return m_vInfluenceScale;
}

void ezBoxReflectionProbeComponent::SetInfluenceScale(const ezVec3& vInfluenceScale)
{
  m_vInfluenceScale = vInfluenceScale;
}

const ezVec3& ezBoxReflectionProbeComponent::GetInfluenceShift() const
{
  return m_vInfluenceShift;
}

void ezBoxReflectionProbeComponent::SetInfluenceShift(const ezVec3& vInfluenceShift)
{
  m_vInfluenceShift = vInfluenceShift;
}

void ezBoxReflectionProbeComponent::SetPositiveFalloff(const ezVec3& vFalloff)
{
  // Does not affect cube generation so m_bStatesDirty is not set.
  m_vPositiveFalloff = vFalloff.CompClamp(ezVec3(ezMath::DefaultEpsilon<float>()), ezVec3(1.0f));
}

void ezBoxReflectionProbeComponent::SetNegativeFalloff(const ezVec3& vFalloff)
{
  // Does not affect cube generation so m_bStatesDirty is not set.
  m_vNegativeFalloff = vFalloff.CompClamp(ezVec3(ezMath::DefaultEpsilon<float>()), ezVec3(1.0f));
}

const ezVec3& ezBoxReflectionProbeComponent::GetExtents() const
{
  return m_vExtents;
}

void ezBoxReflectionProbeComponent::OnActivated()
{
  GetOwner()->EnableStaticTransformChangesNotifications();
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
  msg.SetAlwaysVisible(ezDefaultSpatialDataCategories::RenderDynamic);
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
  pRenderData->m_vProbePosition = pRenderData->m_GlobalTransform * m_desc.m_vCaptureOffset;
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
  pRenderData->m_vInfluenceScale = m_vInfluenceScale;
  pRenderData->m_vInfluenceShift = m_vInfluenceShift;
  pRenderData->m_vPositiveFalloff = m_vPositiveFalloff;
  pRenderData->m_vNegativeFalloff = m_vNegativeFalloff;
  pRenderData->m_Id = m_Id;
  pRenderData->m_uiIndex = 0;
  ezReflectionPool::ExtractReflectionProbe(this, msg, pRenderData, GetWorld(), m_Id, fPriority);
}

void ezBoxReflectionProbeComponent::OnTransformChanged(ezMsgTransformChanged& msg)
{
  m_bStatesDirty = true;
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
