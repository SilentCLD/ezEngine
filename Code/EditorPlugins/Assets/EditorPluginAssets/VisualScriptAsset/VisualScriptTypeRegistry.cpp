#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <Core/Messages/EventMessage.h>
#include <EditorPluginAssets/VisualScriptAsset/VisualScriptGraph.h>
#include <EditorPluginAssets/VisualScriptAsset/VisualScriptGraphQt.moc.h>
#include <EditorPluginAssets/VisualScriptAsset/VisualScriptTypeRegistry.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/SimdMath/SimdRandom.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>

namespace
{
  ezColorGammaUB ColorFromHex(ezUInt32 hex)
  {
    return ezColorGammaUB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
  }

  ezColorGammaUB niceColors[] =
    {
      ColorFromHex(0x008F7A),
      ColorFromHex(0x008E9B),
      ColorFromHex(0x0089BA),
      ColorFromHex(0x0081CF),
      ColorFromHex(0x2C73D2),
      ColorFromHex(0x845EC2),
      ColorFromHex(0xD65DB1),
      ColorFromHex(0xFF6F91),
      ColorFromHex(0xFF9671),
      ColorFromHex(0xFFC75F),
  };

  ezColorGammaUB NiceColorFromFloat(float x)
  {
    x = ezMath::Saturate(x);

    constexpr ezUInt32 uiMaxIndex = (EZ_ARRAY_SIZE(niceColors) - 1);
    const ezUInt32 uiIndexA = ezUInt32(x * uiMaxIndex);
    const ezUInt32 uiIndexB = ezMath::Min(uiIndexA + 1, uiMaxIndex);
    const float fFrac = (x * uiMaxIndex) - uiIndexA;

    ezColor A = niceColors[uiIndexA];
    ezColor B = niceColors[uiIndexB];
    return ezMath::Lerp(A, B, fFrac);
  }

  ezColorGammaUB NiceColorFromString(ezStringView s)
  {
    float x = ezSimdRandom::FloatZeroToOne(ezSimdVec4i(ezHashingUtils::StringHash(s))).x();
    return NiceColorFromFloat(x);
  }

  ezColorGammaUB ClampToMaxValue(ezColor c, float maxValue = 0.7f)
  {
    float hue, saturation, value;
    c.GetHSV(hue, saturation, value);
    c.SetHSV(hue, saturation, ezMath::Min(value, maxValue));
    return c;
  }
} // namespace

EZ_IMPLEMENT_SINGLETON(ezVisualScriptTypeRegistry);

// clang-format off
EZ_BEGIN_SUBSYSTEM_DECLARATION(EditorFramework, VisualScript)

  BEGIN_SUBSYSTEM_DEPENDENCIES
  "PluginAssets", "ReflectedTypeManager"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    EZ_DEFAULT_NEW(ezVisualScriptTypeRegistry);

    ezVisualScriptTypeRegistry::GetSingleton()->UpdateNodeTypes();
    const ezRTTI* pBaseType = ezVisualScriptTypeRegistry::GetSingleton()->GetNodeBaseType();

    ezQtNodeScene::GetPinFactory().RegisterCreator(ezGetStaticRTTI<ezVisualScriptPin>(), [](const ezRTTI* pRtti)->ezQtPin* { return new ezQtVisualScriptPin(); }).IgnoreResult();
    ezQtNodeScene::GetConnectionFactory().RegisterCreator(ezGetStaticRTTI<ezVisualScriptConnection>(), [](const ezRTTI* pRtti)->ezQtConnection* { return new ezQtVisualScriptConnection(); }).IgnoreResult();
    ezQtNodeScene::GetNodeFactory().RegisterCreator(pBaseType, [](const ezRTTI* pRtti)->ezQtNode* { return new ezQtVisualScriptNode(); }).IgnoreResult();
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    ezVisualScriptTypeRegistry* pDummy = ezVisualScriptTypeRegistry::GetSingleton();
    EZ_DEFAULT_DELETE(pDummy);
  }

  ON_HIGHLEVELSYSTEMS_STARTUP
  {
  }

  ON_HIGHLEVELSYSTEMS_SHUTDOWN
  {
  }

EZ_END_SUBSYSTEM_DECLARATION;
// clang-format on

ezVisualScriptTypeRegistry::ezVisualScriptTypeRegistry()
  : m_SingletonRegistrar(this)
{
  m_pBaseType = nullptr;
  ezPhantomRttiManager::s_Events.AddEventHandler(ezMakeDelegate(&ezVisualScriptTypeRegistry::PhantomTypeRegistryEventHandler, this));
}


ezVisualScriptTypeRegistry::~ezVisualScriptTypeRegistry()
{
  ezPhantomRttiManager::s_Events.RemoveEventHandler(ezMakeDelegate(&ezVisualScriptTypeRegistry::PhantomTypeRegistryEventHandler, this));
}

const ezVisualScriptNodeDescriptor* ezVisualScriptTypeRegistry::GetDescriptorForType(const ezRTTI* pRtti) const
{
  auto it = m_NodeDescriptors.Find(pRtti);

  if (!it.IsValid())
    return nullptr;

  return &it.Value();
}

void ezVisualScriptTypeRegistry::PhantomTypeRegistryEventHandler(const ezPhantomRttiManagerEvent& e)
{
  if (e.m_Type == ezPhantomRttiManagerEvent::Type::TypeAdded || e.m_Type == ezPhantomRttiManagerEvent::Type::TypeChanged)
  {
    UpdateNodeType(e.m_pChangedType);
  }
}

void ezVisualScriptTypeRegistry::UpdateNodeTypes()
{
  EZ_PROFILE_SCOPE("UpdateNodeTypes");

  // Base Node Type
  if (m_pBaseType == nullptr)
  {
    ezReflectedTypeDescriptor desc;
    desc.m_sTypeName = "ezVisualScriptNodeBase";
    desc.m_sPluginName = "VisualScriptTypes";
    desc.m_sParentTypeName = ezGetStaticRTTI<ezReflectedClass>()->GetTypeName();
    desc.m_Flags = ezTypeFlags::Phantom | ezTypeFlags::Abstract | ezTypeFlags::Class;
    desc.m_uiTypeSize = 0;
    desc.m_uiTypeVersion = 1;

    m_pBaseType = ezPhantomRttiManager::RegisterType(desc);
  }

  auto& dynEnum = ezDynamicStringEnum::CreateDynamicEnum("ComponentTypes");

  for (const ezRTTI* pRtti = ezRTTI::GetFirstInstance(); pRtti != nullptr; pRtti = pRtti->GetNextInstance())
  {
    if (pRtti->IsDerivedFrom<ezComponent>())
    {
      dynEnum.AddValidValue(pRtti->GetTypeName(), true);
    }
  }

  for (const ezRTTI* pRtti = ezRTTI::GetFirstInstance(); pRtti != nullptr; pRtti = pRtti->GetNextInstance())
  {
    UpdateNodeType(pRtti);
  }
}

static ezColorGammaUB PinTypeColor(ezVisualScriptDataPinType::Enum type)
{
  float x = (float)(type - 1) / (ezVisualScriptDataPinType::Variant - 1);
  return NiceColorFromFloat(x);
}

void ezVisualScriptTypeRegistry::UpdateNodeType(const ezRTTI* pRtti)
{
  if (pRtti->GetAttributeByType<ezHiddenAttribute>() != nullptr)
    return;

  if (pRtti->IsDerivedFrom<ezMessage>() && !pRtti->GetTypeFlags().IsSet(ezTypeFlags::Abstract))
  {
    if (pRtti->GetAttributeByType<ezAutoGenVisScriptMsgSender>())
    {
      CreateMessageSenderNodeType(pRtti);
    }

    if (pRtti->GetAttributeByType<ezAutoGenVisScriptMsgHandler>())
    {
      CreateMessageHandlerNodeType(pRtti);
    }
  }

  // expose reflected functions to visual scripts
  {
    for (const ezAbstractFunctionProperty* pFuncProp : pRtti->GetFunctions())
    {
      CreateFunctionCallNodeType(pRtti, pFuncProp);
    }
  }

  if (!pRtti->IsDerivedFrom<ezVisualScriptNode>() || pRtti->GetTypeFlags().IsAnySet(ezTypeFlags::Abstract))
    return;

  ezHybridArray<ezAbstractProperty*, 32> properties;
  static const ezColor ExecutionPinColor = ezColor::LightSlateGrey;

  ezVisualScriptNodeDescriptor nd;
  nd.m_sTypeName = pRtti->GetTypeName();

  if (const ezCategoryAttribute* pAttr = pRtti->GetAttributeByType<ezCategoryAttribute>())
  {
    nd.m_sCategory = pAttr->GetCategory();
  }

  nd.m_Color = ClampToMaxValue(NiceColorFromString(nd.m_sCategory));

  if (const ezTitleAttribute* pAttr = pRtti->GetAttributeByType<ezTitleAttribute>())
  {
    nd.m_sTitle = pAttr->GetTitle();
  }

  if (const ezColorAttribute* pAttr = pRtti->GetAttributeByType<ezColorAttribute>())
  {
    nd.m_Color = pAttr->GetColor();
  }

  pRtti->GetAllProperties(properties);

  ezSet<ezInt32> usedInputDataPinIDs;
  ezSet<ezInt32> usedOutputDataPinIDs;
  ezSet<ezInt32> usedInputExecPinIDs;
  ezSet<ezInt32> usedOutputExecPinIDs;

  for (auto prop : properties)
  {
    ezVisualScriptPinDescriptor pd;
    pd.m_sName = prop->GetPropertyName();
    pd.m_sTooltip = ""; /// \todo Use ezTranslateTooltip

    if (const ezVisScriptDataPinInAttribute* pAttr = prop->GetAttributeByType<ezVisScriptDataPinInAttribute>())
    {
      pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
      pd.m_Color = PinTypeColor(pAttr->m_DataType);
      pd.m_DataType = pAttr->m_DataType;
      pd.m_uiPinIndex = pAttr->m_uiPinSlot;
      nd.m_InputPins.PushBack(pd);

      if (usedInputDataPinIDs.Contains(pd.m_uiPinIndex))
        ezLog::Error("Visual Script Node '{0}' uses the same input data pin index multiple times: '{1}'", nd.m_sTypeName, pd.m_uiPinIndex);

      usedInputDataPinIDs.Insert(pd.m_uiPinIndex);
    }

    if (const ezVisScriptDataPinOutAttribute* pAttr = prop->GetAttributeByType<ezVisScriptDataPinOutAttribute>())
    {
      pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
      pd.m_Color = PinTypeColor(pAttr->m_DataType);
      pd.m_DataType = pAttr->m_DataType;
      pd.m_uiPinIndex = pAttr->m_uiPinSlot;
      nd.m_OutputPins.PushBack(pd);

      if (usedOutputDataPinIDs.Contains(pd.m_uiPinIndex))
        ezLog::Error("Visual Script Node '{0}' uses the same output data pin index multiple times: '{1}'", nd.m_sTypeName, pd.m_uiPinIndex);

      usedOutputDataPinIDs.Insert(pd.m_uiPinIndex);
    }

    if (const ezVisScriptExecPinInAttribute* pAttr = prop->GetAttributeByType<ezVisScriptExecPinInAttribute>())
    {
      pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
      pd.m_Color = ExecutionPinColor;
      pd.m_uiPinIndex = pAttr->m_uiPinSlot;
      nd.m_InputPins.PushBack(pd);

      if (usedInputExecPinIDs.Contains(pd.m_uiPinIndex))
        ezLog::Error("Visual Script Node '{0}' uses the same input exec pin index multiple times: '{1}'", nd.m_sTypeName, pd.m_uiPinIndex);

      usedInputExecPinIDs.Insert(pd.m_uiPinIndex);
    }

    if (const ezVisScriptExecPinOutAttribute* pAttr = prop->GetAttributeByType<ezVisScriptExecPinOutAttribute>())
    {
      pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
      pd.m_Color = ExecutionPinColor;
      pd.m_uiPinIndex = pAttr->m_uiPinSlot;
      nd.m_OutputPins.PushBack(pd);

      if (usedOutputExecPinIDs.Contains(pd.m_uiPinIndex))
        ezLog::Error("Visual Script Node '{0}' uses the same output exec pin index multiple times: '{1}'", nd.m_sTypeName, pd.m_uiPinIndex);

      usedOutputExecPinIDs.Insert(pd.m_uiPinIndex);
    }

    if (prop->GetCategory() == ezPropertyCategory::Constant)
      continue;

    {
      ezReflectedPropertyDescriptor pd;
      pd.m_Flags = prop->GetFlags();
      pd.m_Category = prop->GetCategory();
      pd.m_sName = prop->GetPropertyName();
      pd.m_sType = prop->GetSpecificType()->GetTypeName();

      for (ezPropertyAttribute* const pAttr : prop->GetAttributes())
      {
        pd.m_Attributes.PushBack(ezReflectionSerializer::Clone(pAttr));
      }

      nd.m_Properties.PushBack(pd);
    }
  }

  m_NodeDescriptors.Insert(GenerateTypeFromDesc(nd), nd);
}

const ezRTTI* ezVisualScriptTypeRegistry::GenerateTypeFromDesc(const ezVisualScriptNodeDescriptor& nd)
{
  ezStringBuilder temp;
  temp.Set("VisualScriptNode::", nd.m_sTypeName);

  ezReflectedTypeDescriptor desc;
  desc.m_sTypeName = temp;
  desc.m_sPluginName = "VisualScriptTypes";
  desc.m_sParentTypeName = m_pBaseType->GetTypeName();
  desc.m_Flags = ezTypeFlags::Phantom | ezTypeFlags::Class;
  desc.m_uiTypeSize = 0;
  desc.m_uiTypeVersion = 1;
  desc.m_Properties = nd.m_Properties;

  return ezPhantomRttiManager::RegisterType(desc);
}

void ezVisualScriptTypeRegistry::CreateMessageSenderNodeType(const ezRTTI* pRtti)
{
  const ezStringBuilder tmp(pRtti->GetTypeName(), "<send>");

  ezVisualScriptNodeDescriptor nd;
  nd.m_sTypeName = tmp;
  nd.m_Color = ClampToMaxValue(NiceColorFromFloat(0.5f));
  nd.m_sCategory = "Message Senders";

  if (const ezCategoryAttribute* pAttr = pRtti->GetAttributeByType<ezCategoryAttribute>())
  {
    nd.m_sCategory = pAttr->GetCategory();
  }

  if (const ezColorAttribute* pAttr = pRtti->GetAttributeByType<ezColorAttribute>())
  {
    nd.m_Color = pAttr->GetColor();
  }

  ezHybridArray<ezAbstractProperty*, 32> properties;
  pRtti->GetAllProperties(properties);

  // Add an input execution pin
  {
    static const ezColor ExecutionPinColor = ezColor::LightSlateGrey;

    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "send";
    pd.m_sTooltip = "When executed, the message is sent to the object or component.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
    pd.m_Color = ExecutionPinColor;
    pd.m_uiPinIndex = 0;
    nd.m_InputPins.PushBack(pd);
  }

  // Add an output execution pin
  {
    static const ezColor ExecutionPinColor = ezColor::LightSlateGrey;

    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "then";
    pd.m_sTooltip = "";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
    pd.m_Color = ExecutionPinColor;
    pd.m_uiPinIndex = 0;
    nd.m_OutputPins.PushBack(pd);
  }

  // Add an input data pin for the target object
  {
    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "Object";
    pd.m_sTooltip = "When the object is given, the message is sent to all its components.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pd.m_DataType = ezVisualScriptDataPinType::GameObjectHandle;
    pd.m_Color = PinTypeColor(ezVisualScriptDataPinType::GameObjectHandle);
    pd.m_uiPinIndex = 0;
    nd.m_InputPins.PushBack(pd);
  }

  // Add an input data pin for the target component
  {
    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "Component";
    pd.m_sTooltip = "When the component is given, the message is sent directly to it.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pd.m_DataType = ezVisualScriptDataPinType::ComponentHandle;
    pd.m_Color = PinTypeColor(ezVisualScriptDataPinType::ComponentHandle);
    pd.m_uiPinIndex = 1;
    nd.m_InputPins.PushBack(pd);
  }

  // Add an input data pin for delay
  {
    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "Delay";
    pd.m_sTooltip = "Delay message send by the given time in seconds.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pd.m_DataType = ezVisualScriptDataPinType::Number;
    pd.m_Color = PinTypeColor(ezVisualScriptDataPinType::Number);
    pd.m_uiPinIndex = 2;
    nd.m_InputPins.PushBack(pd);
  }

  // Delayed Delivery Property
  {
    ezReflectedPropertyDescriptor prd;
    prd.m_Flags = ezPropertyFlags::StandardType | ezPropertyFlags::Phantom;
    prd.m_Category = ezPropertyCategory::Member;
    prd.m_sName = "Delay";
    prd.m_sType = ezGetStaticRTTI<ezTime>()->GetTypeName();

    nd.m_Properties.PushBack(prd);
  }

  // Recursive Delivery Property
  {
    ezReflectedPropertyDescriptor prd;
    prd.m_Flags = ezPropertyFlags::StandardType | ezPropertyFlags::Phantom;
    prd.m_Category = ezPropertyCategory::Member;
    prd.m_sName = "Recursive";
    prd.m_sType = ezGetStaticRTTI<bool>()->GetTypeName();

    nd.m_Properties.PushBack(prd);
  }

  ezInt32 iDataPinIndex = 2; // the first valid index is '3', because of the object, component and delay data pins
  for (auto prop : properties)
  {
    if (prop->GetCategory() == ezPropertyCategory::Constant)
      continue;

    {
      ezReflectedPropertyDescriptor prd;
      prd.m_Flags = prop->GetFlags();
      prd.m_Category = prop->GetCategory();
      prd.m_sName = prop->GetPropertyName();
      prd.m_sType = prop->GetSpecificType()->GetTypeName();

      for (ezPropertyAttribute* const pAttr : prop->GetAttributes())
      {
        prd.m_Attributes.PushBack(ezReflectionSerializer::Clone(pAttr));
      }

      nd.m_Properties.PushBack(prd);
    }

    ++iDataPinIndex;
    auto dataPinType = ezVisualScriptDataPinType::GetDataPinTypeForType(prop->GetSpecificType());
    if (dataPinType == ezVisualScriptDataPinType::None)
      continue;

    ezVisualScriptPinDescriptor pid;
    pid.m_sName = prop->GetPropertyName();
    pid.m_sTooltip = ""; /// \todo Use ezTranslateTooltip
    pid.m_Color = PinTypeColor(dataPinType);
    pid.m_DataType = dataPinType;
    pid.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pid.m_uiPinIndex = iDataPinIndex;
    nd.m_InputPins.PushBack(pid);
  }

  m_NodeDescriptors.Insert(GenerateTypeFromDesc(nd), nd);
}

void ezVisualScriptTypeRegistry::CreateMessageHandlerNodeType(const ezRTTI* pRtti)
{
  const ezStringBuilder tmp(pRtti->GetTypeName(), "<handle>");

  ezVisualScriptNodeDescriptor nd;
  nd.m_sTypeName = tmp;
  nd.m_Color = ClampToMaxValue(NiceColorFromFloat(0.9f));
  nd.m_sCategory = "Message Handlers";

  if (const ezCategoryAttribute* pAttr = pRtti->GetAttributeByType<ezCategoryAttribute>())
  {
    nd.m_sCategory = pAttr->GetCategory();
  }

  if (const ezColorAttribute* pAttr = pRtti->GetAttributeByType<ezColorAttribute>())
  {
    nd.m_Color = pAttr->GetColor();
  }

  ezHybridArray<ezAbstractProperty*, 32> properties;
  pRtti->GetAllProperties(properties);

  // Add an output execution pin
  {
    static const ezColor ExecutionPinColor = ezColor::LightSlateGrey;

    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "OnMsg";
    pd.m_sTooltip = "";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
    pd.m_Color = ExecutionPinColor;
    pd.m_uiPinIndex = 0;
    nd.m_OutputPins.PushBack(pd);
  }

  ezInt32 iDataPinIndex = -1;
  for (auto prop : properties)
  {
    if (prop->GetCategory() == ezPropertyCategory::Constant)
      continue;

    ++iDataPinIndex;
    auto dataPinType = ezVisualScriptDataPinType::GetDataPinTypeForType(prop->GetSpecificType());
    if (dataPinType == ezVisualScriptDataPinType::None)
      continue;

    ezVisualScriptPinDescriptor pid;
    pid.m_sName = prop->GetPropertyName();
    pid.m_sTooltip = ""; /// \todo Use ezTranslateTooltip
    pid.m_Color = PinTypeColor(dataPinType);
    pid.m_DataType = dataPinType;
    pid.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pid.m_uiPinIndex = iDataPinIndex;
    nd.m_OutputPins.PushBack(pid);
  }

  m_NodeDescriptors.Insert(GenerateTypeFromDesc(nd), nd);
}

void ezVisualScriptTypeRegistry::CreateFunctionCallNodeType(const ezRTTI* pRtti, const ezAbstractFunctionProperty* pFunction)
{
  if (pFunction->GetFunctionType() != ezFunctionType::Member)
    return;

  const ezScriptableFunctionAttribute* pAttr = pFunction->GetAttributeByType<ezScriptableFunctionAttribute>();
  if (pAttr == nullptr)
    return;

  ezStringBuilder tmp(pRtti->GetTypeName(), "::", pFunction->GetPropertyName(), "<call>");

  ezVisualScriptNodeDescriptor nd;
  nd.m_sTypeName = tmp;

  tmp.Format("Components/{}", pRtti->GetTypeName());
  nd.m_sCategory = tmp;

  if (const ezCategoryAttribute* pAttr = pFunction->GetAttributeByType<ezCategoryAttribute>())
  {
    nd.m_sCategory = pAttr->GetCategory();
  }

  nd.m_Color = ClampToMaxValue(NiceColorFromString(nd.m_sCategory));

  if (const ezColorAttribute* pAttr = pFunction->GetAttributeByType<ezColorAttribute>())
  {
    nd.m_Color = pAttr->GetColor();
  }

  // Add an input execution pin
  {
    static const ezColor ExecutionPinColor = ezColor::LightSlateGrey;

    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "call";
    pd.m_sTooltip = "When executed, the message is sent to the object or component.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
    pd.m_Color = ExecutionPinColor;
    pd.m_uiPinIndex = 0;
    nd.m_InputPins.PushBack(pd);
  }

  // Add an output execution pin
  {
    static const ezColor ExecutionPinColor = ezColor::LightSlateGrey;

    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "then";
    pd.m_sTooltip = "";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Execution;
    pd.m_Color = ExecutionPinColor;
    pd.m_uiPinIndex = 0;
    nd.m_OutputPins.PushBack(pd);
  }

  // Add an input data pin for the target object
  {
    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "Object";
    pd.m_sTooltip = "When the object is given, the function is called on the first matching component.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pd.m_DataType = ezVisualScriptDataPinType::GameObjectHandle;
    pd.m_Color = PinTypeColor(pd.m_DataType);
    pd.m_uiPinIndex = 0;
    nd.m_InputPins.PushBack(pd);
  }

  // Add an input data pin for the target component
  {
    ezVisualScriptPinDescriptor pd;
    pd.m_sName = "Component";
    pd.m_sTooltip = "When the component is given, the function is called directly on it.";
    pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pd.m_DataType = ezVisualScriptDataPinType::ComponentHandle;
    pd.m_Color = PinTypeColor(pd.m_DataType);
    pd.m_uiPinIndex = 1;
    nd.m_InputPins.PushBack(pd);
  }

  ezInt32 iDataPinIndexOut = 0;

  ezStringBuilder sName;

  if (pFunction->GetReturnType() != nullptr)
  {
    auto dataPinType = ezVisualScriptDataPinType::GetDataPinTypeForType(pFunction->GetReturnType());
    if (dataPinType != ezVisualScriptDataPinType::None)
    {
      tmp.Set(pRtti->GetTypeName(), "::", pFunction->GetPropertyName(), "->Return");

      ezVisualScriptPinDescriptor pd;
      pd.m_sName = "Result";
      pd.m_sTooltip = ezTranslateTooltip(tmp);
      pd.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
      pd.m_DataType = dataPinType;
      pd.m_Color = PinTypeColor(pd.m_DataType);
      pd.m_uiPinIndex = iDataPinIndexOut; // result is always on pin 0
      nd.m_OutputPins.PushBack(pd);

      ++iDataPinIndexOut;
    }
  }

  for (ezUInt32 argIdx = 0; argIdx < pFunction->GetArgumentCount(); ++argIdx)
  {
    if (pFunction->GetArgumentFlags(argIdx).IsAnySet(ezPropertyFlags::StandardType) == false)
    {
      ezLog::Error("Script function '{}' uses non-standard type for argument {}", nd.m_sTypeName, argIdx + 1);
      return;
    }

    sName = pAttr->GetArgumentName(argIdx);
    if (sName.IsEmpty())
      sName.Format("arg{}", argIdx);

    const ezScriptableFunctionAttribute::ArgType argType = pAttr->GetArgumentType(argIdx);

    // add the inputs as properties
    if (argType != ezScriptableFunctionAttribute::Out) // in or inout
    {
      ezReflectedPropertyDescriptor prd;
      prd.m_Flags = pFunction->GetArgumentFlags(argIdx);
      prd.m_Category = ezPropertyCategory::Member;
      prd.m_sName = sName;
      prd.m_sType = pFunction->GetArgumentType(argIdx)->GetTypeName();

      ezVisScriptMappingAttribute* pMappingAttr = ezVisScriptMappingAttribute::GetStaticRTTI()->GetAllocator()->Allocate<ezVisScriptMappingAttribute>();
      pMappingAttr->m_iMapping = argIdx;
      prd.m_Attributes.PushBack(pMappingAttr);

      nd.m_Properties.PushBack(prd);
    }

    auto dataPinType = ezVisualScriptDataPinType::GetDataPinTypeForType(pFunction->GetArgumentType(argIdx));
    if (dataPinType == ezVisualScriptDataPinType::None)
      continue;

    tmp.Set(pRtti->GetTypeName(), "::", pFunction->GetPropertyName(), "->", sName);

    ezVisualScriptPinDescriptor pid;
    pid.m_DataType = dataPinType;
    pid.m_sName = sName;
    pid.m_sTooltip = ezTranslateTooltip(tmp);
    pid.m_PinType = ezVisualScriptPinDescriptor::PinType::Data;
    pid.m_Color = PinTypeColor(pid.m_DataType);
    pid.m_uiPinIndex = 2 + argIdx; // TODO: document what m_uiPinIndex is for

    if (argType != ezScriptableFunctionAttribute::Out) // in or inout
    {
      nd.m_InputPins.PushBack(pid);
    }

    if (argType != ezScriptableFunctionAttribute::In /* out or inout */)
    {
      if (!pFunction->GetArgumentFlags(argIdx).IsSet(ezPropertyFlags::Reference))
      {
        // TODO: ezPropertyFlags::Reference is also set for const-ref parameters, should we change that ?

        ezLog::Error("Script function '{}' argument {} is marked 'out' but is not a non-const reference value", nd.m_sTypeName, argIdx + 1);
        return;
      }

      pid.m_uiPinIndex = iDataPinIndexOut;
      nd.m_OutputPins.PushBack(pid);

      ++iDataPinIndexOut;
    }
  }

  m_NodeDescriptors.Insert(GenerateTypeFromDesc(nd), nd);
}
