#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Foundation/Math/Declarations.h>
#include <PhysXPlugin/Components/PxComponent.h>

struct ezMsgAnimationPoseUpdated;
struct ezMsgPhysicsAddImpulse;
struct ezMsgPhysicsAddForce;
struct ezSkeletonResourceGeometry;
class ezPxUserData;
class ezSkeletonJoint;

namespace physx
{
  class PxAggregate;
  class PxArticulation;
  class PxArticulationDriveCache;
  class PxRigidActor;
  class PxMaterial;
  struct PxFilterData;
} // namespace physx

using ezSkeletonResourceHandle = ezTypedResourceHandle<class ezSkeletonResource>;
using ezSurfaceResourceHandle = ezTypedResourceHandle<class ezSurfaceResource>;

using ezPxRagdollComponentManager = ezComponentManagerSimple<class ezPxRagdollComponent, ezComponentUpdateType::WhenSimulating, ezBlockStorageType::Compact>;

struct ezPxRagdollStart
{
  using StorageType = ezUInt8;

  enum Enum
  {
    BindPose,
    WaitForPose,
    WaitForPoseAndVelocity,
    Wait,
    Default = BindPose
  };
};

EZ_DECLARE_REFLECTABLE_TYPE(EZ_PHYSXPLUGIN_DLL, ezPxRagdollStart);

class EZ_PHYSXPLUGIN_DLL ezPxRagdollComponent : public ezPxComponent
{
  EZ_DECLARE_COMPONENT_TYPE(ezPxRagdollComponent, ezPxComponent, ezPxRagdollComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // ezComponent

public:
  virtual void SerializeComponent(ezWorldWriter& stream) const override;
  virtual void DeserializeComponent(ezWorldReader& stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // ezPxRagdollComponent

public:
  ezPxRagdollComponent();
  ~ezPxRagdollComponent();

  ezUInt32 GetShapeId() const { return m_uiShapeID; } // [ scriptable ]

  void OnAnimationPoseUpdated(ezMsgAnimationPoseUpdated& msg); // [ msg handler ]

  bool GetDisableGravity() const { return m_bDisableGravity; } // [ property ]
  void SetDisableGravity(bool b);                              // [ property ]

  void SetSurfaceFile(const char* szFile); // [ property ]
  const char* GetSurfaceFile() const;      // [ property ]

  ezUInt8 m_uiCollisionLayer = 0; // [ property ]
  bool m_bSelfCollision = false;  // [ property ]

  void AddImpulseAtPos(ezMsgPhysicsAddImpulse& msg); // [ message ]
  void AddForceAtPos(ezMsgPhysicsAddForce& msg);     // [ message ]

protected:
  struct LinkData
  {
    physx::PxArticulationLink* m_pLink = nullptr;
    ezTransform m_GlobalTransform;
  };

  void CreatePhysicsShapes(const ezSkeletonResourceHandle& hSkeleton, ezMsgAnimationPoseUpdated& poseMsg);
  void DestroyPhysicsShapes();
  void UpdatePose();
  void Update();
  void CreateShapesFromBindPose();
  void AddArticulationToScene();
  void CreateBoneShape(const ezTransform& rootTransform, physx::PxRigidActor& actor, const ezSkeletonResourceGeometry& geo, const physx::PxMaterial& pxMaterial, const physx::PxFilterData& pxFilterData, ezPxUserData* pPxUserData);
  void CreateBoneLink(ezUInt16 uiBoneIdx, const ezSkeletonJoint& bone, ezPxUserData* pPxUserData, LinkData& thisLink, const LinkData& parentLink, ezMsgAnimationPoseUpdated& poseMsg);

  physx::PxMaterial* GetPxMaterial();
  physx::PxFilterData CreateFilterData();

  ezSurfaceResourceHandle m_hSurface;

  bool m_bShapesCreated = false;
  bool m_bHasFirstState = false;
  bool m_bDisableGravity = false;
  ezUInt32 m_uiShapeID = ezInvalidIndex;
  ezUInt32 m_uiUserDataIndex = ezInvalidIndex;

  struct ArtLink
  {
    physx::PxArticulationLink* m_pLink = nullptr;
  };

  struct Impulse
  {
    ezVec3 m_vPos;
    ezVec3 m_vImpulse;
  };


  ezHybridArray<Impulse, 8> m_Impulses;

  ezEnum<ezPxRagdollStart> m_Start;
  physx::PxArticulationLink* m_pRootLink = nullptr;
  ezDynamicArray<ArtLink> m_ArticulationLinks;
  //ezDynamicArray<ezVec3> m_vLastPos;

  ezSkeletonResourceHandle m_hSkeleton;
  physx::PxAggregate* m_pAggregate = nullptr;
  physx::PxArticulation* m_pArticulation = nullptr;
};
