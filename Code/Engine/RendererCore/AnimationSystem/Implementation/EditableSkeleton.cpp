#include <RendererCore/RendererCorePCH.h>

#include <RendererCore/AnimationSystem/EditableSkeleton.h>
#include <RendererCore/AnimationSystem/Implementation/OzzUtils.h>
#include <RendererCore/AnimationSystem/SkeletonBuilder.h>
#include <RendererCore/AnimationSystem/SkeletonResource.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/skeleton.h>

// clang-format off
EZ_BEGIN_STATIC_REFLECTED_ENUM(ezSkeletonJointGeometryType, 1)
EZ_ENUM_CONSTANTS(ezSkeletonJointGeometryType::None, ezSkeletonJointGeometryType::Capsule, ezSkeletonJointGeometryType::Sphere, ezSkeletonJointGeometryType::Box)
EZ_END_STATIC_REFLECTED_ENUM;

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezEditableSkeletonBoneShape, 1, ezRTTIDefaultAllocator<ezEditableSkeletonBoneShape>)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ENUM_MEMBER_PROPERTY("Geometry", ezSkeletonJointGeometryType, m_Geometry),
    EZ_MEMBER_PROPERTY("Offset", m_vOffset),
    EZ_MEMBER_PROPERTY("Rotation", m_qRotation),
    EZ_MEMBER_PROPERTY("Length", m_fLength)->AddAttributes(new ezDefaultValueAttribute(0.1f), new ezClampValueAttribute(0.01f, 10.0f)),
    EZ_MEMBER_PROPERTY("Width", m_fWidth)->AddAttributes(new ezDefaultValueAttribute(0.05f), new ezClampValueAttribute(0.01f, 10.0f)),
    EZ_MEMBER_PROPERTY("Thickness", m_fThickness)->AddAttributes(new ezDefaultValueAttribute(0.05f), new ezClampValueAttribute(0.01f, 10.0f)),

    EZ_MEMBER_PROPERTY("OverrideName", m_bOverrideName),
    EZ_MEMBER_PROPERTY("Name", m_sNameOverride),
    EZ_MEMBER_PROPERTY("OverrideSurface", m_bOverrideSurface),
    EZ_MEMBER_PROPERTY("Surface", m_sSurfaceOverride)->AddAttributes(new ezAssetBrowserAttribute("Surface")),
    EZ_MEMBER_PROPERTY("OverrideCollisionLayer", m_bOverrideCollisionLayer),
    EZ_MEMBER_PROPERTY("CollisionLayer", m_uiCollisionLayerOverride)->AddAttributes(new ezDynamicEnumAttribute("PhysicsCollisionLayer")),

  }
  EZ_END_PROPERTIES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezEditableSkeletonJoint, 1, ezRTTIDefaultAllocator<ezEditableSkeletonJoint>)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Name", GetName, SetName),
    EZ_MEMBER_PROPERTY("Transform", m_Transform)->AddFlags(ezPropertyFlags::Hidden)->AddAttributes(new ezDefaultValueAttribute(ezTransform::IdentityTransform())),
    EZ_MEMBER_PROPERTY("GlobalPos", m_vJointPosGlobal)->AddAttributes(new ezHiddenAttribute()),
    EZ_MEMBER_PROPERTY_READ_ONLY("GlobalPosRO", m_vJointPosGlobal)->AddAttributes(new ezHiddenAttribute()),
    EZ_ACCESSOR_PROPERTY("LimitOrientation", GetJointLimitOrientation, SetJointLimitOrientation),
    EZ_MEMBER_PROPERTY("SwingLimitX", m_SwingLimitX)->AddAttributes(new ezClampValueAttribute(ezAngle(), ezAngle::Degree(170)), new ezDefaultValueAttribute(ezAngle::Degree(30))),
    EZ_MEMBER_PROPERTY("SwingLimitY", m_SwingLimitY)->AddAttributes(new ezClampValueAttribute(ezAngle(), ezAngle::Degree(170)), new ezDefaultValueAttribute(ezAngle::Degree(30))),
    EZ_MEMBER_PROPERTY("TwistLimitLow", m_TwistLimitLow)->AddAttributes(new ezClampValueAttribute(ezAngle(), ezAngle::Degree(180)), new ezDefaultValueAttribute(ezAngle::Degree(15))),
    EZ_MEMBER_PROPERTY("TwistLimitHigh", m_TwistLimitHigh)->AddAttributes(new ezClampValueAttribute(ezAngle(), ezAngle::Degree(180)), new ezDefaultValueAttribute(ezAngle::Degree(15))),
    EZ_ARRAY_MEMBER_PROPERTY("Children", m_Children)->AddFlags(ezPropertyFlags::PointerOwner | ezPropertyFlags::Hidden),
    EZ_ARRAY_MEMBER_PROPERTY("BoneShapes", m_BoneShapes),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezTransformManipulatorAttribute("GlobalPosRO", "LimitOrientation"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezEditableSkeleton, 1, ezRTTIDefaultAllocator<ezEditableSkeleton>)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_MEMBER_PROPERTY("File", m_sAnimationFile)->AddAttributes(new ezFileBrowserAttribute("Select Mesh", "*.fbx;*.gltf;*.glb")),
    EZ_ENUM_MEMBER_PROPERTY("RightDir", ezBasisAxis, m_RightDir)->AddAttributes(new ezDefaultValueAttribute((int)ezBasisAxis::PositiveX)),
    EZ_ENUM_MEMBER_PROPERTY("UpDir", ezBasisAxis, m_UpDir)->AddAttributes(new ezDefaultValueAttribute((int)ezBasisAxis::PositiveY)),
    EZ_MEMBER_PROPERTY("FlipForwardDir", m_bFlipForwardDir),
    EZ_MEMBER_PROPERTY("UniformScaling", m_fUniformScaling)->AddAttributes(new ezDefaultValueAttribute(1.0f), new ezClampValueAttribute(0.0001f, 10000.0f)),
    EZ_ENUM_MEMBER_PROPERTY("BoneDirection", ezBasisAxis, m_BoneDirection)->AddAttributes(new ezDefaultValueAttribute((int)ezBasisAxis::PositiveY)),
    EZ_MEMBER_PROPERTY("CollisionLayer", m_uiCollisionLayer)->AddAttributes(new ezDynamicEnumAttribute("PhysicsCollisionLayer")),
    EZ_MEMBER_PROPERTY("Surface", m_sSurfaceFile)->AddAttributes(new ezAssetBrowserAttribute("Surface")),

    EZ_ARRAY_MEMBER_PROPERTY("Children", m_Children)->AddFlags(ezPropertyFlags::PointerOwner | ezPropertyFlags::Hidden),
  }
  EZ_END_PROPERTIES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

ezEditableSkeleton::ezEditableSkeleton() = default;
ezEditableSkeleton::~ezEditableSkeleton()
{
  ClearJoints();
}

void ezEditableSkeleton::ClearJoints()
{
  for (ezEditableSkeletonJoint* pChild : m_Children)
  {
    EZ_DEFAULT_DELETE(pChild);
  }

  m_Children.Clear();
}

void ezEditableSkeleton::AddChildJoints(ezSkeletonBuilder& sb, ezSkeletonResourceDescriptor& desc, const ezEditableSkeletonJoint* pParentJoint, const ezEditableSkeletonJoint* pJoint, ezUInt32 uiJointIdx, const ezQuat& parentRot) const
{
  for (auto& shape : pJoint->m_BoneShapes)
  {
    auto& geo = desc.m_Geometry.ExpandAndGetRef();

    geo.m_Type = shape.m_Geometry;
    geo.m_uiAttachedToJoint = static_cast<ezUInt16>(uiJointIdx);
    geo.m_sName = pJoint->m_sName;
    geo.m_Transform.SetIdentity();
    geo.m_Transform.m_vScale.Set(shape.m_fLength, shape.m_fWidth, shape.m_fThickness);
    geo.m_Transform.m_vPosition = shape.m_vOffset;
    geo.m_Transform.m_qRotation = shape.m_qRotation;
    geo.m_sSurface.Assign(m_sSurfaceFile);
    geo.m_uiCollisionLayer = m_uiCollisionLayer;

    if (shape.m_bOverrideName)
      geo.m_sName.Assign(shape.m_sNameOverride);
    if (shape.m_bOverrideCollisionLayer)
      geo.m_uiCollisionLayer = shape.m_uiCollisionLayerOverride;
    if (shape.m_bOverrideSurface)
      geo.m_sSurface.Assign(shape.m_sSurfaceOverride);
  }

  const ezQuat qThisRot = parentRot * pJoint->m_Transform.m_qRotation;

  for (const auto* pChildJoint : pJoint->m_Children)
  {
    const ezUInt32 idx = sb.AddJoint(pChildJoint->GetName(), pChildJoint->m_Transform, uiJointIdx);

    const ezQuat qLocalLimit = -desc.m_RootTransform.m_qRotation * -parentRot * pChildJoint->m_qJointLimitOrientation;

    sb.SetJointLimit(idx, qLocalLimit, pChildJoint->m_SwingLimitX, pChildJoint->m_SwingLimitY, pChildJoint->m_TwistLimitLow, pChildJoint->m_TwistLimitHigh);

    AddChildJoints(sb, desc, pJoint, pChildJoint, idx, qThisRot);
  }
}

void ezEditableSkeleton::FillResourceDescriptor(ezSkeletonResourceDescriptor& desc) const
{
  desc.m_Geometry.Clear();

  ezSkeletonBuilder sb;
  for (const auto* pJoint : m_Children)
  {
    const ezUInt32 idx = sb.AddJoint(pJoint->GetName(), pJoint->m_Transform);

    sb.SetJointLimit(idx, pJoint->m_qJointLimitOrientation, pJoint->m_SwingLimitX, pJoint->m_SwingLimitY, pJoint->m_TwistLimitLow, pJoint->m_TwistLimitHigh);

    AddChildJoints(sb, desc, nullptr, pJoint, idx, ezQuat::IdentityQuaternion());
  }
  sb.BuildSkeleton(desc.m_Skeleton);
  desc.m_Skeleton.m_BoneDirection = m_BoneDirection;
}

static void BuildOzzRawSkeleton(const ezEditableSkeletonJoint& srcJoint, ozz::animation::offline::RawSkeleton::Joint& dstJoint)
{
  dstJoint.name = srcJoint.m_sName.GetString();
  dstJoint.transform.translation.x = srcJoint.m_Transform.m_vPosition.x;
  dstJoint.transform.translation.y = srcJoint.m_Transform.m_vPosition.y;
  dstJoint.transform.translation.z = srcJoint.m_Transform.m_vPosition.z;
  dstJoint.transform.rotation.x = srcJoint.m_Transform.m_qRotation.v.x;
  dstJoint.transform.rotation.y = srcJoint.m_Transform.m_qRotation.v.y;
  dstJoint.transform.rotation.z = srcJoint.m_Transform.m_qRotation.v.z;
  dstJoint.transform.rotation.w = srcJoint.m_Transform.m_qRotation.w;
  dstJoint.transform.scale.x = srcJoint.m_Transform.m_vScale.x;
  dstJoint.transform.scale.y = srcJoint.m_Transform.m_vScale.y;
  dstJoint.transform.scale.z = srcJoint.m_Transform.m_vScale.z;

  dstJoint.children.resize((size_t)srcJoint.m_Children.GetCount());

  for (ezUInt32 b = 0; b < srcJoint.m_Children.GetCount(); ++b)
  {
    BuildOzzRawSkeleton(*srcJoint.m_Children[b], dstJoint.children[b]);
  }
}

void ezEditableSkeleton::GenerateRawOzzSkeleton(ozz::animation::offline::RawSkeleton& out_Skeleton) const
{
  out_Skeleton.roots.resize((size_t)m_Children.GetCount());

  for (ezUInt32 b = 0; b < m_Children.GetCount(); ++b)
  {
    BuildOzzRawSkeleton(*m_Children[b], out_Skeleton.roots[b]);
  }
}

void ezEditableSkeleton::GenerateOzzSkeleton(ozz::animation::Skeleton& out_Skeleton) const
{
  ozz::animation::offline::RawSkeleton rawSkeleton;
  GenerateRawOzzSkeleton(rawSkeleton);

  ozz::animation::offline::SkeletonBuilder skeletonBuilder;
  auto pNewOzzSkeleton = skeletonBuilder(rawSkeleton);

  ezOzzUtils::CopySkeleton(&out_Skeleton, pNewOzzSkeleton.get());
}

ezEditableSkeletonJoint::ezEditableSkeletonJoint() = default;

ezEditableSkeletonJoint::~ezEditableSkeletonJoint()
{
  ClearJoints();
}

const char* ezEditableSkeletonJoint::GetName() const
{
  return m_sName.GetData();
}

void ezEditableSkeletonJoint::SetName(const char* sz)
{
  m_sName.Assign(sz);
}

void ezEditableSkeletonJoint::ClearJoints()
{
  for (ezEditableSkeletonJoint* pChild : m_Children)
  {
    EZ_DEFAULT_DELETE(pChild);
  }
  m_Children.Clear();
}

void ezEditableSkeletonJoint::CopyPropertiesFrom(const ezEditableSkeletonJoint* pJoint)
{
  // do not copy:
  //  name
  //  transform
  //  children

  m_BoneShapes = pJoint->m_BoneShapes;
  m_qJointLimitOrientation = pJoint->m_qJointLimitOrientation;
  m_SwingLimitX = pJoint->m_SwingLimitX;
  m_SwingLimitY = pJoint->m_SwingLimitY;
  m_TwistLimitLow = pJoint->m_TwistLimitLow;
  m_TwistLimitHigh = pJoint->m_TwistLimitHigh;
}

ezVec3 ezEditableSkeletonJoint::GetJointPosGlobal() const
{
  return m_vJointPosGlobal;
}

ezQuat ezEditableSkeletonJoint::GetJointLimitOrientation() const
{
  return m_qJointLimitOrientation;
}

void ezEditableSkeletonJoint::SetJointLimitOrientation(ezQuat val)
{
  m_qJointLimitOrientation = val;
}

EZ_STATICLINK_FILE(RendererCore, RendererCore_AnimationSystem_Implementation_EditableSkeleton);
