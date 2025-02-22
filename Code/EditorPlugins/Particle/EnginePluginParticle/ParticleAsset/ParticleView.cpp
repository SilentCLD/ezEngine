#include <EnginePluginParticle/EnginePluginParticlePCH.h>

#include <EnginePluginParticle/ParticleAsset/ParticleContext.h>
#include <EnginePluginParticle/ParticleAsset/ParticleView.h>
#include <RendererCore/Pipeline/View.h>

ezParticleViewContext::ezParticleViewContext(ezParticleContext* pParticleContext)
  : ezEngineProcessViewContext(pParticleContext)
{
  m_pParticleContext = pParticleContext;
}

ezParticleViewContext::~ezParticleViewContext() {}

void ezParticleViewContext::PositionThumbnailCamera(const ezBoundingBoxSphere& bounds)
{
  m_Camera.SetCameraMode(ezCameraMode::PerspectiveFixedFovX, 45.0f, 0.1f, 1000.0f);

  FocusCameraOnObject(m_Camera, bounds, 45.0f, -ezVec3(-1.8f, 1.8f, 1.0f));
}

ezViewHandle ezParticleViewContext::CreateView()
{
  ezView* pView = nullptr;
  ezRenderWorld::CreateView("Particle Editor - View", pView);

  pView->SetRenderPipelineResource(CreateDefaultRenderPipeline());

  ezEngineProcessDocumentContext* pDocumentContext = GetDocumentContext();
  pView->SetWorld(pDocumentContext->GetWorld());
  pView->SetCamera(&m_Camera);
  return pView->GetHandle();
}
