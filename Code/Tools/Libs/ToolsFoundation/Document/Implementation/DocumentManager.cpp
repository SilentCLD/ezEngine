#include <ToolsFoundation/ToolsFoundationPCH.h>

#include <Foundation/Configuration/Plugin.h>
#include <Foundation/Configuration/SubSystem.h>
#include <Foundation/IO/FileSystem/DeferredFileWriter.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Profiling/Profiling.h>
#include <Foundation/Serialization/DdlSerializer.h>
#include <Foundation/Serialization/RttiConverter.h>
#include <ToolsFoundation/Document/DocumentManager.h>
#include <ToolsFoundation/Document/DocumentUtils.h>
#include <ToolsFoundation/Project/ToolsProject.h>

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezDocumentManager, 1, ezRTTINoAllocator)
EZ_END_DYNAMIC_REFLECTED_TYPE;

EZ_BEGIN_SUBSYSTEM_DECLARATION(ToolsFoundation, DocumentManager)

  BEGIN_SUBSYSTEM_DEPENDENCIES
  "Foundation"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    ezPlugin::Events().AddEventHandler(ezDocumentManager::OnPluginEvent);
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    ezPlugin::Events().RemoveEventHandler(ezDocumentManager::OnPluginEvent);
  }

EZ_END_SUBSYSTEM_DECLARATION;
// clang-format on

ezSet<const ezRTTI*> ezDocumentManager::s_KnownManagers;
ezHybridArray<ezDocumentManager*, 16> ezDocumentManager::s_AllDocumentManagers;
ezMap<ezString, const ezDocumentTypeDescriptor*> ezDocumentManager::s_AllDocumentDescriptors; // maps from "sDocumentTypeName" to descriptor
ezCopyOnBroadcastEvent<const ezDocumentManager::Event&> ezDocumentManager::s_Events;
ezEvent<ezDocumentManager::Request&> ezDocumentManager::s_Requests;

void ezDocumentManager::OnPluginEvent(const ezPluginEvent& e)
{
  switch (e.m_EventType)
  {
    case ezPluginEvent::BeforeUnloading:
      UpdateBeforeUnloadingPlugins(e);
      break;
    case ezPluginEvent::AfterPluginChanges:
      UpdatedAfterLoadingPlugins();
      break;

    default:
      break;
  }
}

void ezDocumentManager::UpdateBeforeUnloadingPlugins(const ezPluginEvent& e)
{
  bool bChanges = false;

  // triggers a reevaluation next time
  s_AllDocumentDescriptors.Clear();

  // remove all document managers that belong to this plugin
  for (ezUInt32 i = 0; i < s_AllDocumentManagers.GetCount();)
  {
    const ezRTTI* pRtti = s_AllDocumentManagers[i]->GetDynamicRTTI();

    if (ezStringUtils::IsEqual(pRtti->GetPluginName(), e.m_szPluginBinary))
    {
      s_KnownManagers.Remove(pRtti);

      pRtti->GetAllocator()->Deallocate(s_AllDocumentManagers[i]);
      s_AllDocumentManagers.RemoveAtAndSwap(i);

      bChanges = true;
    }
    else
      ++i;
  }

  if (bChanges)
  {
    Event e2;
    e2.m_Type = Event::Type::DocumentTypesRemoved;
    s_Events.Broadcast(e2);
  }
}

void ezDocumentManager::UpdatedAfterLoadingPlugins()
{
  bool bChanges = false;

  ezRTTI* pRtti = ezRTTI::GetFirstInstance();

  while (pRtti)
  {
    // find all types derived from ezDocumentManager
    if (pRtti->IsDerivedFrom<ezDocumentManager>())
    {
      // add the ones that we don't know yet
      if (!s_KnownManagers.Find(pRtti).IsValid())
      {
        // add it as 'known' even if we cannot allocate it
        s_KnownManagers.Insert(pRtti);

        if (pRtti->GetAllocator()->CanAllocate())
        {
          // create one instance of each manager type
          ezDocumentManager* pManager = pRtti->GetAllocator()->Allocate<ezDocumentManager>();
          s_AllDocumentManagers.PushBack(pManager);

          bChanges = true;
        }
      }
    }

    pRtti = pRtti->GetNextInstance();
  }

  // triggers a reevaluation next time
  s_AllDocumentDescriptors.Clear();
  GetAllDocumentDescriptors();


  if (bChanges)
  {
    Event e;
    e.m_Type = Event::Type::DocumentTypesAdded;
    s_Events.Broadcast(e);
  }
}

void ezDocumentManager::GetSupportedDocumentTypes(ezDynamicArray<const ezDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  InternalGetSupportedDocumentTypes(inout_DocumentTypes);

  for (auto& dt : inout_DocumentTypes)
  {
    EZ_ASSERT_DEBUG(dt->m_pDocumentType != nullptr, "No document type is set");
    EZ_ASSERT_DEBUG(!dt->m_sFileExtension.IsEmpty(), "File extension must be valid");
    EZ_ASSERT_DEBUG(dt->m_pManager != nullptr, "Document manager must be set");
  }
}

ezStatus ezDocumentManager::CanOpenDocument(const char* szFilePath) const
{
  ezHybridArray<const ezDocumentTypeDescriptor*, 4> DocumentTypes;
  GetSupportedDocumentTypes(DocumentTypes);

  ezStringBuilder sPath = szFilePath;
  ezStringBuilder sExt = sPath.GetFileExtension();

  // check whether the file extension is in the list of possible extensions
  // if not, we can definitely not open this file
  for (ezUInt32 i = 0; i < DocumentTypes.GetCount(); ++i)
  {
    if (DocumentTypes[i]->m_sFileExtension.IsEqual_NoCase(sExt))
    {
      return ezStatus(EZ_SUCCESS);
    }
  }

  return ezStatus("File extension is not handled by any registered type");
}

void ezDocumentManager::EnsureWindowRequested(ezDocument* pDocument, const ezDocumentObject* pOpenContext /*= nullptr*/)
{
  if (pDocument->m_bWindowRequested)
    return;

  EZ_PROFILE_SCOPE("EnsureWindowRequested");
  pDocument->m_bWindowRequested = true;

  Event e;
  e.m_pDocument = pDocument;
  e.m_Type = Event::Type::DocumentWindowRequested;
  e.m_pOpenContext = pOpenContext;
  s_Events.Broadcast(e);

  e.m_pDocument = pDocument;
  e.m_Type = Event::Type::AfterDocumentWindowRequested;
  e.m_pOpenContext = pOpenContext;
  s_Events.Broadcast(e);
}

ezStatus ezDocumentManager::CreateOrOpenDocument(bool bCreate, const char* szDocumentTypeName, const char* szPath, ezDocument*& out_pDocument,
  ezBitflags<ezDocumentFlags> flags, const ezDocumentObject* pOpenContext /*= nullptr*/)
{
  ezFileStats fs;
  ezStringBuilder sPath = szPath;
  sPath.MakeCleanPath();
  if (!bCreate && ezOSFile::GetFileStats(sPath, fs).Failed())
  {
    return ezStatus("The file does not exist.");
  }

  Request r;
  r.m_Type = Request::Type::DocumentAllowedToOpen;
  r.m_RequestStatus.m_Result = EZ_SUCCESS;
  r.m_sDocumentType = szDocumentTypeName;
  r.m_sDocumentPath = sPath;
  s_Requests.Broadcast(r);

  // if for example no project is open, or not the correct one, then a document cannot be opened
  if (r.m_RequestStatus.m_Result.Failed())
    return r.m_RequestStatus;

  out_pDocument = nullptr;

  ezStatus status;

  ezHybridArray<const ezDocumentTypeDescriptor*, 4> DocumentTypes;
  GetSupportedDocumentTypes(DocumentTypes);

  for (ezUInt32 i = 0; i < DocumentTypes.GetCount(); ++i)
  {
    if (DocumentTypes[i]->m_sDocumentTypeName == szDocumentTypeName)
    {
      // See if there is a default asset document registered for the type, if so clone
      // it and use that as the new document instead of creating one from scratch.
      if (bCreate)
      {
        ezString ProjectDirectory = ezToolsProject::GetSingleton()->GetProjectDirectory();
        ezStringBuilder TemplateDocumentPath = ProjectDirectory;
        ezStringBuilder DocumentFileName;
        TemplateDocumentPath.AppendPath("DocumentTemplates", "Default");
        ezStringBuilder Temp;
        TemplateDocumentPath.ChangeFileExtension(sPath.GetFileExtension().GetData(Temp));

        if (ezOSFile::ExistsFile(TemplateDocumentPath))
        {
          EZ_PROFILE_SCOPE(szDocumentTypeName);

          ezUuid CloneUuid;
          if (CloneDocument(TemplateDocumentPath, sPath, CloneUuid).Succeeded())
          {
            status = OpenDocument(szDocumentTypeName, sPath, out_pDocument, flags, pOpenContext);

            if (status.Failed())
            {
              ezLog::SeriousWarning("Couldn't open cloned template document, proceeding with normal creation behavior.");
            }
            else
            {
              return status;
            }
          }
          else
          {
            ezLog::SeriousWarning("Couldn't clone template document, proceeding with normal creation behavior.");
          }
        }
      }

      EZ_ASSERT_DEV(DocumentTypes[i]->m_bCanCreate, "This document manager cannot create the document type '{0}'", szDocumentTypeName);

      {
        EZ_PROFILE_SCOPE(szDocumentTypeName);
        status = ezStatus(EZ_SUCCESS);
        InternalCreateDocument(szDocumentTypeName, sPath, bCreate, out_pDocument, pOpenContext);
      }
      out_pDocument->SetAddToResetFilesList(flags.IsSet(ezDocumentFlags::AddToRecentFilesList));

      if (status.m_Result.Succeeded())
      {
        out_pDocument->SetupDocumentInfo(DocumentTypes[i]);

        out_pDocument->m_pDocumentManager = this;
        m_AllOpenDocuments.PushBack(out_pDocument);

        if (!bCreate)
        {
          status = out_pDocument->LoadDocument();
        }

        {
          EZ_PROFILE_SCOPE("InitializeAfterLoading");
          out_pDocument->InitializeAfterLoading(bCreate);
        }

        if (bCreate)
        {
          out_pDocument->SetModified(true);
          if (flags.IsSet(ezDocumentFlags::AsyncSave))
          {
            out_pDocument->SaveDocumentAsync({});
            status = ezStatus(EZ_SUCCESS);
          }
          else
          {
            status = out_pDocument->SaveDocument();
          }
        }

        {
          EZ_PROFILE_SCOPE("InitializeAfterLoadingAndSaving");
          out_pDocument->InitializeAfterLoadingAndSaving();
        }

        Event e;
        e.m_pDocument = out_pDocument;
        e.m_Type = Event::Type::DocumentOpened;

        s_Events.Broadcast(e);

        if (flags.IsSet(ezDocumentFlags::RequestWindow))
          EnsureWindowRequested(out_pDocument, pOpenContext);
      }

      return status;
    }
  }

  EZ_REPORT_FAILURE("This document manager does not support the document type '{0}'", szDocumentTypeName);
  return status;
}

ezStatus ezDocumentManager::CreateDocument(
  const char* szDocumentTypeName, const char* szPath, ezDocument*& out_pDocument, ezBitflags<ezDocumentFlags> flags, const ezDocumentObject* pOpenContext)
{
  return CreateOrOpenDocument(true, szDocumentTypeName, szPath, out_pDocument, flags, pOpenContext);
}

ezStatus ezDocumentManager::OpenDocument(const char* szDocumentTypeName, const char* szPath, ezDocument*& out_pDocument,
  ezBitflags<ezDocumentFlags> flags, const ezDocumentObject* pOpenContext)
{
  return CreateOrOpenDocument(false, szDocumentTypeName, szPath, out_pDocument, flags, pOpenContext);
}


ezStatus ezDocumentManager::CloneDocument(const char* szPath, const char* szClonePath, ezUuid& inout_cloneGuid)
{
  const ezDocumentTypeDescriptor* pTypeDesc = nullptr;
  ezStatus res = ezDocumentUtils::IsValidSaveLocationForDocument(szClonePath, &pTypeDesc);
  if (res.Failed())
    return res;

  ezUniquePtr<ezAbstractObjectGraph> header;
  ezUniquePtr<ezAbstractObjectGraph> objects;
  ezUniquePtr<ezAbstractObjectGraph> types;

  res = ezDocument::ReadDocument(szPath, header, objects, types);
  if (res.Failed())
    return res;

  ezUuid documentId;
  ezAbstractObjectNode::Property* documentIdProp = nullptr;
  {
    ezRttiConverterContext context;
    ezRttiConverterReader rttiConverter(header.Borrow(), &context);
    auto* pHeaderNode = header->GetNodeByName("Header");
    EZ_ASSERT_DEV(pHeaderNode, "No header found, document '{0}' is corrupted.", szPath);
    documentIdProp = pHeaderNode->FindProperty("DocumentID");
    EZ_ASSERT_DEV(documentIdProp, "No document ID property found in header, document document '{0}' is corrupted.", szPath);
    documentId = documentIdProp->m_Value.Get<ezUuid>();
  }

  ezUuid seedGuid;
  if (inout_cloneGuid.IsValid())
  {
    seedGuid = inout_cloneGuid;
    seedGuid.RevertCombinationWithSeed(documentId);

    ezUuid test = documentId;
    test.CombineWithSeed(seedGuid);
    EZ_ASSERT_DEV(test == inout_cloneGuid, "");
  }
  else
  {
    seedGuid.CreateNewUuid();
    inout_cloneGuid = documentId;
    inout_cloneGuid.CombineWithSeed(seedGuid);
  }

  {
    // Remap
    header->ReMapNodeGuids(seedGuid);
    objects->ReMapNodeGuids(seedGuid);
    documentIdProp->m_Value = inout_cloneGuid;

    // Fix cloning of docs containing prefabs.
    // TODO: generalize this for other doc features?
    auto& AllNodes = objects->GetAllNodes();
    for (auto it = AllNodes.GetIterator(); it.IsValid(); ++it)
    {
      auto* pNode = it.Value();
      ezAbstractObjectNode::Property* pProp = pNode->FindProperty("MetaPrefabSeed");
      if (pProp && pProp->m_Value.IsA<ezUuid>())
      {
        ezUuid prefabSeed = pProp->m_Value.Get<ezUuid>();
        prefabSeed.CombineWithSeed(seedGuid);
        pProp->m_Value = prefabSeed;
      }
    }
  }

  {
    ezDeferredFileWriter file;
    file.SetOutput(szClonePath);
    ezAbstractGraphDdlSerializer::WriteDocument(file, header.Borrow(), objects.Borrow(), types.Borrow(), false);
    if (file.Close() == EZ_FAILURE)
    {
      return ezStatus(ezFmt("Unable to open file '{0}' for writing!", szClonePath));
    }
  }
  return ezStatus(EZ_SUCCESS);
}

void ezDocumentManager::CloseDocument(ezDocument* pDocument)
{
  EZ_ASSERT_DEV(pDocument != nullptr, "Invalid document pointer");

  if (!m_AllOpenDocuments.RemoveAndCopy(pDocument))
    return;

  Event e;
  e.m_pDocument = pDocument;
  e.m_Type = Event::Type::DocumentClosing;
  s_Events.Broadcast(e);

  e.m_pDocument = pDocument;
  e.m_Type = Event::Type::DocumentClosing2;
  s_Events.Broadcast(e);

  delete pDocument;

  e.m_pDocument = pDocument;
  e.m_Type = Event::Type::DocumentClosed;
  s_Events.Broadcast(e);
}

void ezDocumentManager::CloseAllDocumentsOfManager()
{
  while (!m_AllOpenDocuments.IsEmpty())
  {
    CloseDocument(m_AllOpenDocuments[0]);
  }
}

void ezDocumentManager::CloseAllDocuments()
{
  for (ezDocumentManager* pMan : s_AllDocumentManagers)
  {
    pMan->CloseAllDocumentsOfManager();
  }
}

ezDocument* ezDocumentManager::GetDocumentByPath(const char* szPath) const
{
  ezStringBuilder sPath = szPath;
  sPath.MakeCleanPath();

  for (ezDocument* pDoc : m_AllOpenDocuments)
  {
    if (sPath.IsEqual_NoCase(pDoc->GetDocumentPath()))
      return pDoc;
  }

  return nullptr;
}


ezDocument* ezDocumentManager::GetDocumentByGuid(const ezUuid& guid)
{
  for (auto man : s_AllDocumentManagers)
  {
    for (auto doc : man->m_AllOpenDocuments)
    {
      if (doc->GetGuid() == guid)
        return doc;
    }
  }

  return nullptr;
}


bool ezDocumentManager::EnsureDocumentIsClosedInAllManagers(const char* szPath)
{
  bool bClosedAny = false;
  for (auto man : s_AllDocumentManagers)
  {
    if (man->EnsureDocumentIsClosed(szPath))
      bClosedAny = true;
  }

  return bClosedAny;
}

bool ezDocumentManager::EnsureDocumentIsClosed(const char* szPath)
{
  auto pDoc = GetDocumentByPath(szPath);

  if (pDoc == nullptr)
    return false;

  CloseDocument(pDoc);

  return true;
}

ezResult ezDocumentManager::FindDocumentTypeFromPath(const char* szPath, bool bForCreation, const ezDocumentTypeDescriptor*& out_pTypeDesc)
{
  const ezString sFileExt = ezPathUtils::GetFileExtension(szPath);

  const auto& allDesc = GetAllDocumentDescriptors();

  for (auto it : allDesc)
  {
    const auto* desc = it.Value();

    if (bForCreation && !desc->m_bCanCreate)
      continue;

    if (desc->m_sFileExtension.IsEqual_NoCase(sFileExt))
    {
      out_pTypeDesc = desc;
      return EZ_SUCCESS;
    }
  }

  return EZ_FAILURE;
}

const ezMap<ezString, const ezDocumentTypeDescriptor*>& ezDocumentManager::GetAllDocumentDescriptors()
{
  if (s_AllDocumentDescriptors.IsEmpty())
  {
    for (ezDocumentManager* pMan : ezDocumentManager::GetAllDocumentManagers())
    {
      ezHybridArray<const ezDocumentTypeDescriptor*, 4> descriptors;
      pMan->GetSupportedDocumentTypes(descriptors);

      for (auto pDesc : descriptors)
      {
        s_AllDocumentDescriptors[pDesc->m_sDocumentTypeName] = pDesc;
      }
    }
  }

  return s_AllDocumentDescriptors;
}

const ezDocumentTypeDescriptor* ezDocumentManager::GetDescriptorForDocumentType(const char* szDocumentType)
{
  return GetAllDocumentDescriptors().GetValueOrDefault(szDocumentType, nullptr);
}

/// \todo on close doc: remove from m_AllDocuments
