#include <RendererCore/RendererCorePCH.h>

#include <Core/Graphics/Geometry.h>
#include <Core/Messages/SetColorMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Configuration/CVar.h>
#include <RendererCore/AnimationSystem/Declarations.h>
#include <RendererCore/Components/RopeRenderComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/Meshes/SkinnedMeshComponent.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <RendererCore/Shader/Types.h>
#include <RendererFoundation/Device/Device.h>

ezCVarBool cvar_FeatureRopesVisBones("Feature.Ropes.VisBones", false, ezCVarFlags::Default, "Enables debug visualization of rope bones");

// clang-format off
EZ_BEGIN_COMPONENT_TYPE(ezRopeRenderComponent, 2, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Material", GetMaterialFile, SetMaterialFile)->AddAttributes(new ezAssetBrowserAttribute("Material")),
    EZ_MEMBER_PROPERTY("Color", m_Color)->AddAttributes(new ezDefaultValueAttribute(ezColor::White), new ezExposeColorAlphaAttribute()),
    EZ_ACCESSOR_PROPERTY("Thickness", GetThickness, SetThickness)->AddAttributes(new ezDefaultValueAttribute(0.05f), new ezClampValueAttribute(0.0f, ezVariant())),
    EZ_ACCESSOR_PROPERTY("Detail", GetDetail, SetDetail)->AddAttributes(new ezDefaultValueAttribute(6), new ezClampValueAttribute(3, 16)),
    EZ_ACCESSOR_PROPERTY("Subdivide", GetSubdivide, SetSubdivide),
    EZ_ACCESSOR_PROPERTY("UScale", GetUScale, SetUScale)->AddAttributes(new ezDefaultValueAttribute(1.0f)),
  }
  EZ_END_PROPERTIES;
  EZ_BEGIN_MESSAGEHANDLERS
  {
    EZ_MESSAGE_HANDLER(ezMsgExtractRenderData, OnMsgExtractRenderData),
    EZ_MESSAGE_HANDLER(ezMsgRopePoseUpdated, OnRopePoseUpdated),
    EZ_MESSAGE_HANDLER(ezMsgSetColor, OnMsgSetColor),
    EZ_MESSAGE_HANDLER(ezMsgSetMeshMaterial, OnMsgSetMeshMaterial),      
  }
  EZ_END_MESSAGEHANDLERS;
  EZ_BEGIN_ATTRIBUTES
  {
    new ezCategoryAttribute("Effects"),
  }
  EZ_END_ATTRIBUTES;
}
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

ezRopeRenderComponent::ezRopeRenderComponent() = default;
ezRopeRenderComponent::~ezRopeRenderComponent() = default;

void ezRopeRenderComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);
  auto& s = stream.GetStream();

  s << m_Color;
  s << m_hMaterial;
  s << m_fThickness;
  s << m_uiDetail;
  s << m_bSubdivide;
  s << m_fUScale;
}

void ezRopeRenderComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& s = stream.GetStream();

  s >> m_Color;
  s >> m_hMaterial;
  s >> m_fThickness;
  s >> m_uiDetail;
  s >> m_bSubdivide;
  s >> m_fUScale;
}

void ezRopeRenderComponent::OnActivated()
{
  SUPER::OnActivated();

  m_LocalBounds.SetInvalid();
}

void ezRopeRenderComponent::OnDeactivated()
{
  if (!m_hSkinningTransformsBuffer.IsInvalidated())
  {
    ezGALDevice::GetDefaultDevice()->DestroyBuffer(m_hSkinningTransformsBuffer);
    m_hSkinningTransformsBuffer.Invalidate();
  }

  SUPER::OnDeactivated();
}

ezResult ezRopeRenderComponent::GetLocalBounds(ezBoundingBoxSphere& bounds, bool& bAlwaysVisible)
{
  bounds = m_LocalBounds;
  return EZ_SUCCESS;
}

void ezRopeRenderComponent::OnMsgExtractRenderData(ezMsgExtractRenderData& msg) const
{
  if (!m_hMesh.IsValid())
    return;

  const ezUInt32 uiFlipWinding = GetOwner()->GetGlobalTransformSimd().ContainsNegativeScale() ? 1 : 0;
  const ezUInt32 uiUniformScale = GetOwner()->GetGlobalTransformSimd().ContainsUniformScale() ? 1 : 0;

  ezResourceLock<ezMeshResource> pMesh(m_hMesh, ezResourceAcquireMode::AllowLoadingFallback);
  ezMaterialResourceHandle hMaterial = m_hMaterial.IsValid() ? m_hMaterial : pMesh->GetMaterials()[0];

  ezSkinnedMeshRenderData* pRenderData = ezCreateRenderDataForThisFrame<ezSkinnedMeshRenderData>(GetOwner());
  {
    pRenderData->m_GlobalTransform = GetOwner()->GetGlobalTransform();
    pRenderData->m_GlobalBounds = GetOwner()->GetGlobalBounds();
    pRenderData->m_hMesh = m_hMesh;
    pRenderData->m_hMaterial = hMaterial;
    pRenderData->m_Color = m_Color;

    pRenderData->m_uiSubMeshIndex = 0;
    pRenderData->m_uiFlipWinding = uiFlipWinding;
    pRenderData->m_uiUniformScale = uiUniformScale;

    pRenderData->m_uiUniqueID = GetUniqueIdForRendering();

    pRenderData->m_hSkinningTransforms = m_hSkinningTransformsBuffer;
    if (ezRenderWorld::GetFrameCounter() <= m_uiSkinningTransformsExtractedFrame)
    {
      auto pSkinningMatrices = EZ_NEW_ARRAY(ezFrameAllocator::GetCurrentAllocator(), ezShaderTransform, m_SkinningTransforms.GetCount());
      pSkinningMatrices.CopyFrom(m_SkinningTransforms);

      pRenderData->m_pNewSkinningTransformData = pSkinningMatrices.ToByteArray();
      m_uiSkinningTransformsExtractedFrame = ezRenderWorld::GetFrameCounter();
    }

    pRenderData->FillBatchIdAndSortingKey();
  }

  // Determine render data category.
  ezRenderData::Category category = ezDefaultRenderDataCategories::LitOpaque;

  if (hMaterial.IsValid())
  {
    ezResourceLock<ezMaterialResource> pMaterial(hMaterial, ezResourceAcquireMode::AllowLoadingFallback);

    ezTempHashedString blendModeValue = pMaterial->GetPermutationValue("BLEND_MODE");
    if (blendModeValue == "BLEND_MODE_OPAQUE" || blendModeValue == "")
    {
      category = ezDefaultRenderDataCategories::LitOpaque;
    }
    else if (blendModeValue == "BLEND_MODE_MASKED")
    {
      category = ezDefaultRenderDataCategories::LitMasked;
    }
    else
    {
      category = ezDefaultRenderDataCategories::LitTransparent;
    }
  }

  msg.AddRenderData(pRenderData, category, ezRenderData::Caching::Never);

  if (cvar_FeatureRopesVisBones)
  {
    ezHybridArray<ezDebugRenderer::Line, 128> lines(ezFrameAllocator::GetCurrentAllocator());
    lines.Reserve(m_SkinningTransforms.GetCount() * 3);

    ezMat4 offsetMat;
    offsetMat.SetIdentity();

    for (ezUInt32 i = 0; i < m_SkinningTransforms.GetCount(); ++i)
    {
      offsetMat.SetTranslationVector(ezVec3(static_cast<float>(i), 0, 0));
      ezMat4 skinningMat = m_SkinningTransforms[i].GetAsMat4() * offsetMat;

      ezVec3 pos = skinningMat.GetTranslationVector();

      auto& x = lines.ExpandAndGetRef();
      x.m_start = pos;
      x.m_end = x.m_start + skinningMat.TransformDirection(ezVec3::UnitXAxis());
      x.m_startColor = ezColor::Red;
      x.m_endColor = ezColor::Red;

      auto& y = lines.ExpandAndGetRef();
      y.m_start = pos;
      y.m_end = y.m_start + skinningMat.TransformDirection(ezVec3::UnitYAxis() * 2.0f);
      y.m_startColor = ezColor::Green;
      y.m_endColor = ezColor::Green;

      auto& z = lines.ExpandAndGetRef();
      z.m_start = pos;
      z.m_end = z.m_start + skinningMat.TransformDirection(ezVec3::UnitZAxis() * 2.0f);
      z.m_startColor = ezColor::Blue;
      z.m_endColor = ezColor::Blue;
    }

    ezDebugRenderer::DrawLines(msg.m_pView->GetHandle(), lines, ezColor::White, GetOwner()->GetGlobalTransform());
  }
}

void ezRopeRenderComponent::SetMaterialFile(const char* szFile)
{
  if (!ezStringUtils::IsNullOrEmpty(szFile))
  {
    m_hMaterial = ezResourceManager::LoadResource<ezMaterialResource>(szFile);
  }
  else
  {
    m_hMaterial.Invalidate();
  }
}

const char* ezRopeRenderComponent::GetMaterialFile() const
{
  if (!m_hMaterial.IsValid())
    return "";

  return m_hMaterial.GetResourceID();
}

void ezRopeRenderComponent::SetThickness(float fThickness)
{
  if (m_fThickness != fThickness)
  {
    m_fThickness = fThickness;

    if (IsActiveAndInitialized() && !m_SkinningTransforms.IsEmpty())
    {
      ezHybridArray<ezTransform, 128> transforms;
      transforms.SetCountUninitialized(m_SkinningTransforms.GetCount());

      ezMat4 offsetMat;
      offsetMat.SetIdentity();

      for (ezUInt32 i = 0; i < m_SkinningTransforms.GetCount(); ++i)
      {
        offsetMat.SetTranslationVector(ezVec3(static_cast<float>(i), 0, 0));
        ezMat4 skinningMat = m_SkinningTransforms[i].GetAsMat4() * offsetMat;

        transforms[i].SetFromMat4(skinningMat);
      }

      UpdateSkinningTransformBuffer(transforms);
    }
  }
}

void ezRopeRenderComponent::SetDetail(ezUInt32 uiDetail)
{
  if (m_uiDetail != uiDetail)
  {
    m_uiDetail = uiDetail;

    if (IsActiveAndInitialized() && !m_SkinningTransforms.IsEmpty())
    {
      GenerateRenderMesh(m_SkinningTransforms.GetCount());
    }
  }
}

void ezRopeRenderComponent::SetSubdivide(bool bSubdivide)
{
  if (m_bSubdivide != bSubdivide)
  {
    m_bSubdivide = bSubdivide;

    if (IsActiveAndInitialized() && !m_SkinningTransforms.IsEmpty())
    {
      GenerateRenderMesh(m_SkinningTransforms.GetCount());
    }
  }
}

void ezRopeRenderComponent::SetUScale(float fUScale)
{
  if (m_fUScale != fUScale)
  {
    m_fUScale = fUScale;

    if (IsActiveAndInitialized() && !m_SkinningTransforms.IsEmpty())
    {
      GenerateRenderMesh(m_SkinningTransforms.GetCount());
    }
  }
}

void ezRopeRenderComponent::OnMsgSetColor(ezMsgSetColor& msg)
{
  msg.ModifyColor(m_Color);
}

void ezRopeRenderComponent::OnMsgSetMeshMaterial(ezMsgSetMeshMaterial& msg)
{
  SetMaterial(msg.m_hMaterial);
}

void ezRopeRenderComponent::OnRopePoseUpdated(ezMsgRopePoseUpdated& msg)
{
  if (msg.m_LinkTransforms.IsEmpty())
    return;

  if (m_SkinningTransforms.GetCount() != msg.m_LinkTransforms.GetCount())
  {
    GenerateRenderMesh(msg.m_LinkTransforms.GetCount());

    if (!m_hSkinningTransformsBuffer.IsInvalidated())
    {
      ezGALDevice::GetDefaultDevice()->DestroyBuffer(m_hSkinningTransformsBuffer);
      m_hSkinningTransformsBuffer.Invalidate();
    }
  }

  UpdateSkinningTransformBuffer(msg.m_LinkTransforms);

  ezBoundingSphere newBounds;
  newBounds.SetFromPoints(&msg.m_LinkTransforms[0].m_vPosition, msg.m_LinkTransforms.GetCount(), sizeof(ezTransform));

  // if the existing bounds are big enough, don't update them
  if (!m_LocalBounds.IsValid() || !m_LocalBounds.GetSphere().Contains(newBounds))
  {
    m_LocalBounds.ExpandToInclude(newBounds);

    TriggerLocalBoundsUpdate();
  }
}

void ezRopeRenderComponent::GenerateRenderMesh(ezUInt32 uiNumRopePieces)
{
  ezStringBuilder sResourceName;
  sResourceName.Format("Rope-Mesh:{}{}-d{}-u{}", uiNumRopePieces, m_bSubdivide ? "Sub" : "", m_uiDetail, m_fUScale);

  m_hMesh = ezResourceManager::GetExistingResource<ezMeshResource>(sResourceName);
  if (m_hMesh.IsValid())
    return;

  ezGeometry geom;

  const ezAngle fDegStep = ezAngle::Degree(360.0f / m_uiDetail);
  const float fVStep = 1.0f / m_uiDetail;

  auto addCap = [&](float x, const ezVec3& normal, ezUInt16 boneIndex, bool flipWinding) {
    ezVec4U16 boneIndices(boneIndex, 0, 0, 0);

    ezUInt32 centerIndex = geom.AddVertex(ezVec3(x, 0, 0), normal, ezVec2(0.5f, 0.5f), ezColor::White, boneIndices);

    ezAngle deg = ezAngle::Radian(0);
    for (ezUInt32 s = 0; s < m_uiDetail; ++s)
    {
      const float fY = ezMath::Cos(deg);
      const float fZ = ezMath::Sin(deg);

      geom.AddVertex(ezVec3(x, fY, fZ), normal, ezVec2(fY, fZ), ezColor::White, boneIndices);

      deg += fDegStep;
    }

    ezUInt32 triangle[3];
    triangle[0] = centerIndex;
    for (ezUInt32 s = 0; s < m_uiDetail; ++s)
    {
      triangle[1] = s + triangle[0] + 1;
      triangle[2] = ((s + 1) % m_uiDetail) + triangle[0] + 1;

      geom.AddPolygon(triangle, flipWinding);
    }
  };

  auto addPiece = [&](float x, const ezVec4U16& boneIndices, const ezColorLinearUB& boneWeights, bool createPolygons) {
    ezAngle deg = ezAngle::Radian(0);
    float fU = x * m_fUScale;
    float fV = 0;

    for (ezUInt32 s = 0; s <= m_uiDetail; ++s)
    {
      const float fY = ezMath::Cos(deg);
      const float fZ = ezMath::Sin(deg);

      const ezVec3 pos(x, fY, fZ);
      const ezVec3 normal(0, fY, fZ);

      geom.AddVertex(pos, normal, ezVec2(fU, fV), ezColor::White, boneIndices, boneWeights);

      deg += fDegStep;
      fV += fVStep;
    }

    if (createPolygons)
    {
      ezUInt32 endIndex = geom.GetVertices().GetCount() - (m_uiDetail + 1);
      ezUInt32 startIndex = endIndex - (m_uiDetail + 1);

      ezUInt32 triangle[3];
      for (ezUInt32 s = 0; s < m_uiDetail; ++s)
      {
        triangle[0] = startIndex + s;
        triangle[1] = startIndex + s + 1;
        triangle[2] = endIndex + s + 1;
        geom.AddPolygon(triangle, false);

        triangle[0] = startIndex + s;
        triangle[1] = endIndex + s + 1;
        triangle[2] = endIndex + s;
        geom.AddPolygon(triangle, false);
      }
    }
  };

  // cap
  {
    const ezVec3 normal = ezVec3(-1, 0, 0);
    addCap(0.0f, normal, 0, true);
  }

  // pieces
  {
    // first ring full weight to first bone
    addPiece(0.0f, ezVec4U16(0, 0, 0, 0), ezColorLinearUB(255, 0, 0, 0), false);

    ezUInt32 p = 1;

    if (m_bSubdivide)
    {
      addPiece(0.75f, ezVec4U16(0, 0, 0, 0), ezColorLinearUB(255, 0, 0, 0), true);

      for (; p < uiNumRopePieces - 2; ++p)
      {
        addPiece(static_cast<float>(p) + 0.25f, ezVec4U16(p, 0, 0, 0), ezColorLinearUB(255, 0, 0, 0), true);
        addPiece(static_cast<float>(p) + 0.75f, ezVec4U16(p, 0, 0, 0), ezColorLinearUB(255, 0, 0, 0), true);
      }

      addPiece(static_cast<float>(p) + 0.25f, ezVec4U16(p, 0, 0, 0), ezColorLinearUB(255, 0, 0, 0), true);
      ++p;
    }
    else
    {
      for (; p < uiNumRopePieces - 1; ++p)
      {
        // Middle rings half weight between bones. To ensure that weights sum up to 1 we weight one bone with 128 and the other with 127,
        // since "ubyte normalized" can't represent 0.5 perfectly.
        addPiece(static_cast<float>(p), ezVec4U16(p - 1, p, 0, 0), ezColorLinearUB(128, 127, 0, 0), true);
      }
    }

    // last ring full weight to last bone
    addPiece(static_cast<float>(p), ezVec4U16(p, 0, 0, 0), ezColorLinearUB(255, 0, 0, 0), true);
  }

  // cap
  {
    const ezVec3 normal = ezVec3(1, 0, 0);
    addCap(static_cast<float>(uiNumRopePieces - 1), normal, uiNumRopePieces - 1, false);
  }

  geom.ComputeTangents();

  ezMeshResourceDescriptor desc;

  // Data/Base/Materials/Prototyping/PrototypeBlack.ezMaterialAsset
  desc.SetMaterial(0, "{ d615cd66-0904-00ca-81f9-768ff4fc24ee }");

  auto& meshBufferDesc = desc.MeshBufferDesc();
  meshBufferDesc.AddCommonStreams();
  meshBufferDesc.AddStream(ezGALVertexAttributeSemantic::BoneIndices0, ezGALResourceFormat::RGBAUByte);
  meshBufferDesc.AddStream(ezGALVertexAttributeSemantic::BoneWeights0, ezGALResourceFormat::RGBAUByteNormalized);
  meshBufferDesc.AllocateStreamsFromGeometry(geom, ezGALPrimitiveTopology::Triangles);

  desc.AddSubMesh(meshBufferDesc.GetPrimitiveCount(), 0, 0);

  desc.ComputeBounds();

  m_hMesh = ezResourceManager::CreateResource<ezMeshResource>(sResourceName, std::move(desc), sResourceName);
}

void ezRopeRenderComponent::UpdateSkinningTransformBuffer(ezArrayPtr<const ezTransform> skinningTransforms)
{
  ezMat4 bindPoseMat;
  bindPoseMat.SetIdentity();
  m_SkinningTransforms.SetCountUninitialized(skinningTransforms.GetCount());

  const ezVec3 newScale = ezVec3(1.0f, m_fThickness * 0.5f, m_fThickness * 0.5f);
  for (ezUInt32 i = 0; i < skinningTransforms.GetCount(); ++i)
  {
    ezTransform t = skinningTransforms[i];
    t.m_vScale = newScale;

    // scale x axis to match the distance between this bone and the next bone
    if (i < skinningTransforms.GetCount() - 1)
    {
      t.m_vScale.x = (skinningTransforms[i + 1].m_vPosition - skinningTransforms[i].m_vPosition).GetLength();
    }

    bindPoseMat.SetTranslationVector(ezVec3(-static_cast<float>(i), 0, 0));

    m_SkinningTransforms[i] = t.GetAsMat4() * bindPoseMat;
  }

  if (m_hSkinningTransformsBuffer.IsInvalidated())
  {
    ezGALBufferCreationDescription BufferDesc;
    BufferDesc.m_uiStructSize = sizeof(ezShaderTransform);
    BufferDesc.m_uiTotalSize = BufferDesc.m_uiStructSize * m_SkinningTransforms.GetCount();
    BufferDesc.m_bUseAsStructuredBuffer = true;
    BufferDesc.m_bAllowShaderResourceView = true;
    BufferDesc.m_ResourceAccess.m_bImmutable = false;

    m_hSkinningTransformsBuffer = ezGALDevice::GetDefaultDevice()->CreateBuffer(BufferDesc, m_SkinningTransforms.GetByteArrayPtr());
    m_uiSkinningTransformsExtractedFrame = 0;
  }
  else
  {
    m_uiSkinningTransformsExtractedFrame = -1;
  }
}
