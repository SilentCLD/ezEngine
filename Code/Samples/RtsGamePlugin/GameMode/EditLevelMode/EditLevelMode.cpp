#include <RtsGamePlugin/RtsGamePluginPCH.h>

#include <Core/Messages/SetColorMessage.h>
#include <Core/Utils/Blackboard.h>
#include <RmlUiPlugin/Components/RmlUiCanvas2DComponent.h>
#include <RmlUiPlugin/RmlUiContext.h>
#include <RtsGamePlugin/GameMode/EditLevelMode/EditLevelMode.h>
#include <RtsGamePlugin/GameState/RtsGameState.h>

const char* g_BuildItemTypes[] = {
  "FederationShip1",
  "FederationShip2",
  "FederationShip3",
  "KlingonShip1",
  "KlingonShip2",
  "KlingonShip3",
};

static ezHashedString s_sTeam = ezMakeHashedString("Team");
static ezHashedString s_sShipType = ezMakeHashedString("ShipType");
static ezHashedString s_sSelectKey = ezMakeHashedString("SelectKey");
static ezHashedString s_sCreateKey = ezMakeHashedString("CreateKey");
static ezHashedString s_sRemoveKey = ezMakeHashedString("RemoveKey");

RtsEditLevelMode::RtsEditLevelMode()
{
  m_pBlackboard = EZ_DEFAULT_NEW(ezBlackboard);
  m_pBlackboard->SetName("EditLevelModel");

  m_pBlackboard->RegisterEntry(s_sTeam, 0);
  m_pBlackboard->RegisterEntry(s_sShipType, 0);
  m_pBlackboard->RegisterEntry(s_sSelectKey, ezVariant());
  m_pBlackboard->RegisterEntry(s_sCreateKey, ezVariant());
  m_pBlackboard->RegisterEntry(s_sRemoveKey, ezVariant());
}

RtsEditLevelMode::~RtsEditLevelMode() = default;

void RtsEditLevelMode::OnActivateMode()
{
  SetupEditUI();
}

void RtsEditLevelMode::OnDeactivateMode()
{
  EZ_LOCK(m_pMainWorld->GetWriteMarker());

  ezRmlUiCanvas2DComponent* pUiComponent = nullptr;
  if (m_pMainWorld->TryGetComponent(m_hEditUIComponent, pUiComponent))
  {
    pUiComponent->SetActiveFlag(false);
  }
}

void RtsEditLevelMode::OnBeforeWorldUpdate()
{
  DisplaySelectModeUI();
  DisplayEditUI();

  m_pGameState->RenderUnitSelection();
}

void RtsEditLevelMode::SetupEditUI()
{
  // Set blackboard values
  {
    m_pBlackboard->SetEntryValue(s_sSelectKey, ezInputManager::GetInputSlotDisplayName(ezInputSlot_MouseButton0)).IgnoreResult();
    m_pBlackboard->SetEntryValue(s_sCreateKey, ezInputManager::GetInputSlotDisplayName("EditLevelMode", "PlaceObject")).IgnoreResult();
    m_pBlackboard->SetEntryValue(s_sRemoveKey, ezInputManager::GetInputSlotDisplayName("EditLevelMode", "RemoveObject")).IgnoreResult();
  }

  if (m_hEditUIComponent.IsInvalidated())
  {
    ezGameObject* pEditUIObject = nullptr;
    if (!m_pMainWorld->TryGetObjectWithGlobalKey(ezTempHashedString("EditUI"), pEditUIObject))
      return;

    ezRmlUiCanvas2DComponent* pUiComponent = nullptr;
    if (!pEditUIObject->TryGetComponentOfBaseType(pUiComponent))
      return;

    pUiComponent->AddBlackboardBinding(*m_pBlackboard);

    m_hEditUIComponent = pUiComponent->GetHandle();
  }
}

void RtsEditLevelMode::DisplayEditUI()
{
  ezRmlUiCanvas2DComponent* pUiComponent = nullptr;
  if (m_pMainWorld->TryGetComponent(m_hEditUIComponent, pUiComponent))
  {
    pUiComponent->SetActiveFlag(s_bUseRmlUi);
  }

  if (!s_bUseRmlUi)
  {
    ezImgui::GetSingleton()->SetCurrentContextForView(m_hMainView);

    const ezSizeU32 resolution = ezImgui::GetSingleton()->GetCurrentWindowResolution();

    const float ww = 200;

    ImGui::SetNextWindowPos(ImVec2((float)resolution.width - ww - 10, 10));
    ImGui::SetNextWindowSize(ImVec2(ww, 150));
    ImGui::Begin("Edit Level", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    int iTeam = m_pBlackboard->GetEntryValue(s_sTeam).Get<int>();
    if (ImGui::Combo("Team", &iTeam, "Red\0Green\0Blue\0Yellow\0\0", 4))
    {
      m_pBlackboard->SetEntryValue(s_sTeam, iTeam).IgnoreResult();
    }

    int iShipType = m_pBlackboard->GetEntryValue(s_sShipType).Get<int>();
    if (ImGui::Combo("Build", &iShipType, g_BuildItemTypes, EZ_ARRAY_SIZE(g_BuildItemTypes)))
    {
      m_pBlackboard->SetEntryValue(s_sShipType, iShipType).IgnoreResult();
    }

    ImGui::Text("Select: %s", ezInputManager::GetInputSlotDisplayName(ezInputSlot_MouseButton0));
    ImGui::Text("Create: %s", ezInputManager::GetInputSlotDisplayName("EditLevelMode", "PlaceObject"));
    ImGui::Text("Remove: %s", ezInputManager::GetInputSlotDisplayName("EditLevelMode", "RemoveObject"));

    ImGui::End();
  }
}

void RtsEditLevelMode::RegisterInputActions()
{
  ezInputActionConfig cfg;

  // Level Editing
  {
    cfg.m_sInputSlotTrigger[0] = ezInputSlot_KeySpace;
    ezInputManager::SetInputActionConfig("EditLevelMode", "PlaceObject", cfg, true);

    cfg.m_sInputSlotTrigger[0] = ezInputSlot_KeyDelete;
    ezInputManager::SetInputActionConfig("EditLevelMode", "RemoveObject", cfg, true);
  }
}

void RtsEditLevelMode::OnProcessInput(const RtsMouseInputState& MouseInput)
{
  DoDefaultCameraInput(MouseInput);

  ezVec3 vPickedGroundPlanePos;
  if (m_pGameState->PickGroundPlanePosition(vPickedGroundPlanePos).Failed())
    return;

  if (ezInputManager::GetInputActionState("EditLevelMode", "PlaceObject") == ezKeyState::Pressed)
  {
    ezGameObject* pSpawned = nullptr;

    int iTeam = m_pBlackboard->GetEntryValue(s_sTeam).Get<int>();
    int iShipType = m_pBlackboard->GetEntryValue(s_sShipType).Get<int>();
    pSpawned = m_pGameState->SpawnNamedObjectAt(ezTransform(vPickedGroundPlanePos, ezQuat::IdentityQuaternion()), g_BuildItemTypes[iShipType], iTeam);

    ezMsgSetColor msg;
    msg.m_Color = RtsGameMode::GetTeamColor(iTeam);

    pSpawned->PostMessageRecursive(msg, ezTime::Zero(), ezObjectMsgQueueType::AfterInitialized);

    return;
  }

  auto& unitSelection = m_pGameState->m_SelectedUnits;

  if (ezInputManager::GetInputActionState("EditLevelMode", "RemoveObject") == ezKeyState::Pressed)
  {
    for (ezUInt32 i = 0; i < unitSelection.GetCount(); ++i)
    {
      ezGameObjectHandle hObject = unitSelection.GetObject(i);
      m_pMainWorld->DeleteObjectDelayed(hObject);
    }

    return;
  }

  m_pGameState->DetectHoveredSelectable();

  if (MouseInput.m_LeftClickState == ezKeyState::Released)
  {
    m_pGameState->SelectUnits();
  }
}
