#include <GameEngine/GameEnginePCH.h>

#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <GameEngine/Gameplay/AreaDamageComponent.h>
#include <GameEngine/Messages/DamageMessage.h>

// clang-format off
EZ_BEGIN_COMPONENT_TYPE(ezAreaDamageComponent, 1, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_MEMBER_PROPERTY("OnCreation", m_bTriggerOnCreation)->AddAttributes(new ezDefaultValueAttribute(true)),
    EZ_MEMBER_PROPERTY("Radius", m_fRadius)->AddAttributes(new ezDefaultValueAttribute(5.0f), new ezClampValueAttribute(0.0f, ezVariant())),
    EZ_MEMBER_PROPERTY("CollisionLayer", m_uiCollisionLayer)->AddAttributes(new ezDynamicEnumAttribute("PhysicsCollisionLayer")),
    EZ_MEMBER_PROPERTY("Damage", m_fDamage)->AddAttributes(new ezDefaultValueAttribute(10.0f)),
    EZ_MEMBER_PROPERTY("Impulse", m_fImpulse)->AddAttributes(new ezDefaultValueAttribute(100.0f)),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_FUNCTIONS
  {
    EZ_SCRIPT_FUNCTION_PROPERTY(ApplyAreaDamage),
  }
  EZ_END_FUNCTIONS;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezCategoryAttribute("Gameplay"),
    new ezSphereVisualizerAttribute("Radius", ezColor::OrangeRed),
    new ezSphereManipulatorAttribute("Radius"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

static ezPhysicsOverlapResultArray g_OverlapResults;

ezAreaDamageComponent::ezAreaDamageComponent() = default;
ezAreaDamageComponent::~ezAreaDamageComponent() = default;

void ezAreaDamageComponent::ApplyAreaDamage()
{
  if (!IsActiveAndSimulating())
    return;

  EZ_PROFILE_SCOPE("ApplyAreaDamage");

  ezPhysicsWorldModuleInterface* pPhysicsInterface = GetWorld()->GetOrCreateModule<ezPhysicsWorldModuleInterface>();

  if (pPhysicsInterface == nullptr)
    return;

  const ezVec3 vOwnPosition = GetOwner()->GetGlobalPosition();

  pPhysicsInterface->QueryShapesInSphere(g_OverlapResults, m_fRadius, vOwnPosition, ezPhysicsQueryParameters(m_uiCollisionLayer));

  const float fInvRadius = 1.0f / m_fRadius;

  for (const auto& hit : g_OverlapResults.m_Results)
  {
    if (!hit.m_hActorObject.IsInvalidated())
    {
      ezGameObject* pObject = nullptr;
      if (GetWorld()->TryGetObject(hit.m_hActorObject, pObject))
      {
        const ezVec3 vTargetPos = pObject->GetGlobalPosition();
        const ezVec3 vDistToTarget = vTargetPos - vOwnPosition;
        ezVec3 vDirToTarget = vDistToTarget;
        const float fDistance = vDirToTarget.GetLength();

        if (fDistance >= 0.01f)
        {
          vDirToTarget /= fDistance;
        }
        else
        {
          vDirToTarget.CreateRandomDirection(GetWorld()->GetRandomNumberGenerator());
        }

        // linearly scale damage and impulse down by distance
        const float fScale = 1.0f - ezMath::Min(fDistance * fInvRadius, 1.0f);

        // apply a physical impulse
        if (m_fImpulse != 0.0f)
        {
          ezMsgPhysicsAddImpulse msg;
          msg.m_vGlobalPosition = vTargetPos;
          msg.m_vImpulse = vDirToTarget * m_fImpulse * fScale;
          msg.m_uiShapeId = hit.m_uiShapeId;

          pObject->SendMessage(msg);
        }

        // apply damage
        if (m_fDamage != 0.0f)
        {
          ezMsgDamage msg;
          msg.m_fDamage = static_cast<double>(m_fDamage) * static_cast<double>(fScale);

          ezGameObject* pShape = nullptr;
          if (GetWorld()->TryGetObject(hit.m_hShapeObject, pShape))
          {
            msg.m_sHitObjectName = pShape->GetName();
          }

          // delay the damage a little bit for nicer chain reactions
          pObject->PostEventMessage(msg, this, ezTime::Milliseconds(200));
        }
      }
    }
  }
}

void ezAreaDamageComponent::OnSimulationStarted()
{
  if (m_bTriggerOnCreation)
  {
    ApplyAreaDamage();
  }
}

void ezAreaDamageComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);
  auto& s = stream.GetStream();

  s << m_bTriggerOnCreation;
  s << m_fRadius;
  s << m_uiCollisionLayer;
  s << m_fDamage;
  s << m_fImpulse;
}

void ezAreaDamageComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  // const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = stream.GetStream();

  s >> m_bTriggerOnCreation;
  s >> m_fRadius;
  s >> m_uiCollisionLayer;
  s >> m_fDamage;
  s >> m_fImpulse;
}

//////////////////////////////////////////////////////////////////////////

ezAreaDamageComponentManager::ezAreaDamageComponentManager(ezWorld* pWorld)
  : SUPER(pWorld)
  , m_pPhysicsInterface(nullptr)
{
}

void ezAreaDamageComponentManager::Initialize()
{
  SUPER::Initialize();

  m_pPhysicsInterface = GetWorld()->GetOrCreateModule<ezPhysicsWorldModuleInterface>();
}

EZ_STATICLINK_FILE(GameEngine, GameEngine_Gameplay_Implementation_AreaDamageComponent);
