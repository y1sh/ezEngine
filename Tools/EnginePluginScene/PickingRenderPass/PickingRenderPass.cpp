#include <PCH.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <EnginePluginScene/PickingRenderPass/PickingRenderPass.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererFoundation/Context/Context.h>
#include <RendererCore/Meshes/MeshRenderer.h>
#include <EnginePluginScene/SceneContext/SceneContext.h>

EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezPickingRenderPass, 1, ezRTTIDefaultAllocator<ezPickingRenderPass>);
EZ_BEGIN_PROPERTIES
EZ_MEMBER_PROPERTY("Enable", m_bEnable),
EZ_MEMBER_PROPERTY("PickSelected", m_bPickSelected),
EZ_MEMBER_PROPERTY("PickingPosition", m_PickingPosition),
EZ_ENUM_MEMBER_PROPERTY("ViewRenderMode", ezViewRenderMode, m_ViewRenderMode),
EZ_ACCESSOR_PROPERTY("SceneContext", GetSceneContext, SetSceneContext),
EZ_END_PROPERTIES
EZ_END_DYNAMIC_REFLECTED_TYPE();

ezPickingRenderPass::ezPickingRenderPass() : ezRenderPipelinePass("EditorPickingRenderPass")
{
  m_pSceneContext = nullptr;
  m_bEnable = true;
  m_bPickSelected = true;
  AddRenderer(EZ_DEFAULT_NEW(ezMeshRenderer));
}

ezPickingRenderPass::~ezPickingRenderPass()
{
  DestroyTarget();
}

ezGALTextureHandle ezPickingRenderPass::GetPickingIdRT() const
{
  return m_hPickingIdRT;
}

ezGALTextureHandle ezPickingRenderPass::GetPickingDepthRT() const
{
  return m_hPickingDepthRT;
}

bool ezPickingRenderPass::GetRenderTargetDescriptions(const ezView& view, const ezArrayPtr<ezGALTextureCreationDescription*const> inputs, ezArrayPtr<ezGALTextureCreationDescription> outputs)
{
  DestroyTarget();
  CreateTarget(view.GetViewport());

  return true;
}

void ezPickingRenderPass::SetRenderTargets(const ezArrayPtr<ezRenderPipelinePassConnection*const> inputs, const ezArrayPtr<ezRenderPipelinePassConnection*const> outputs)
{

}

void ezPickingRenderPass::Execute(const ezRenderViewContext& renderViewContext)
{
  if (!m_bEnable)
    return;

  switch (m_ViewRenderMode)
  {
  case ezViewRenderMode::WireframeColor:
    renderViewContext.m_pRenderContext->SetShaderPermutationVariable("EDITOR_RENDER_MODE", "ERM_WIREFRAME_COLOR");
    break;
  case ezViewRenderMode::WireframeMonochrome:
    renderViewContext.m_pRenderContext->SetShaderPermutationVariable("EDITOR_RENDER_MODE", "ERM_WIREFRAME_MONOCHROME");
    break;
  default:
    renderViewContext.m_pRenderContext->SetShaderPermutationVariable("EDITOR_RENDER_MODE", "ERM_DEFAULT");
    break;
  }

  m_uiWindowWidth = (ezUInt32)renderViewContext.m_pViewData->m_ViewPortRect.width;
  m_uiWindowHeight = (ezUInt32)renderViewContext.m_pViewData->m_ViewPortRect.height;

  // since typically the fov is tied to the height, we orient the gizmo size on that
  const float fGizmoScale = 128.0f / (float)m_uiWindowHeight;
  ezRenderContext::GetDefaultInstance()->SetMaterialParameter("GizmoScale", fGizmoScale);


  ezGALContext* pGALContext = renderViewContext.m_pRenderContext->GetGALContext();

  pGALContext->SetRenderTargetSetup(m_RenderTargetSetup);
  pGALContext->Clear(ezColor(0.0f, 0.0f, 0.0f, 0.0f));

  renderViewContext.m_pRenderContext->SetShaderPermutationVariable("PICKING", "1");
  renderViewContext.m_pRenderContext->SetShaderPermutationVariable("PICKING_IGNORE_GIZMOS", !m_bPickSelected ? "1" : "0");

  RenderDataWithCategory(renderViewContext, ezDefaultRenderDataCategories::Opaque);
  RenderDataWithCategory(renderViewContext, ezDefaultRenderDataCategories::Masked);

  if (m_bPickSelected)
  {
    RenderDataWithCategory(renderViewContext, ezDefaultRenderDataCategories::Selection);
    m_pSceneContext->RenderShapeIcons(renderViewContext.m_pRenderContext);
  }


  {
    {
      // download the picking information from the GPU
      if (m_uiWindowWidth != 0 && m_uiWindowHeight != 0)
      {
        ezGALDevice::GetDefaultDevice()->GetPrimaryContext()->ReadbackTexture(GetPickingDepthRT());

        ezMat4 mProj, mView;



        renderViewContext.m_pCamera->GetProjectionMatrix((float)m_uiWindowWidth / m_uiWindowHeight, mProj);
        renderViewContext.m_pCamera->GetViewMatrix(mView);

        if (mProj.IsNaN())
          return;

        m_PickingInverseViewProjectionMatrix = (mProj * mView).GetInverse();

        m_PickingResultsDepth.Clear();
        m_PickingResultsDepth.SetCount(m_uiWindowWidth * m_uiWindowHeight);

        ezGALSystemMemoryDescription MemDesc;
        MemDesc.m_uiRowPitch = 4 * m_uiWindowWidth;
        MemDesc.m_uiSlicePitch = 4 * m_uiWindowWidth * m_uiWindowHeight;

        MemDesc.m_pData = m_PickingResultsDepth.GetData();
        ezArrayPtr<ezGALSystemMemoryDescription> SysMemDescsDepth(&MemDesc, 1);
        ezGALDevice::GetDefaultDevice()->GetPrimaryContext()->CopyTextureReadbackResult(GetPickingDepthRT(), &SysMemDescsDepth);
      }
    }
  }
  pGALContext->Clear(ezColor(0.0f, 0.0f, 0.0f, 0.0f), 0); // only clear depth

  RenderDataWithCategory(renderViewContext, ezDefaultRenderDataCategories::Foreground1);
  RenderDataWithCategory(renderViewContext, ezDefaultRenderDataCategories::Foreground2);

  renderViewContext.m_pRenderContext->SetShaderPermutationVariable("PICKING", "0");
  renderViewContext.m_pRenderContext->SetShaderPermutationVariable("EDITOR_RENDER_MODE", "ERM_DEFAULT");


  {
    // download the picking information from the GPU
    if (m_uiWindowWidth != 0 && m_uiWindowHeight != 0)
    {
      ezGALDevice::GetDefaultDevice()->GetPrimaryContext()->ReadbackTexture(GetPickingIdRT());

      ezMat4 mProj, mView;

      renderViewContext.m_pCamera->GetProjectionMatrix((float)m_uiWindowWidth / m_uiWindowHeight, mProj);
      renderViewContext.m_pCamera->GetViewMatrix(mView);

      if (mProj.IsNaN())
        return;

      m_PickingInverseViewProjectionMatrix = (mProj * mView).GetInverse();

      m_PickingResultsID.Clear();
      m_PickingResultsID.SetCount(m_uiWindowWidth * m_uiWindowHeight);

      ezGALSystemMemoryDescription MemDesc;
      MemDesc.m_uiRowPitch = 4 * m_uiWindowWidth;
      MemDesc.m_uiSlicePitch = 4 * m_uiWindowWidth * m_uiWindowHeight;

      MemDesc.m_pData = m_PickingResultsID.GetData();
      ezArrayPtr<ezGALSystemMemoryDescription> SysMemDescs(&MemDesc, 1);
      ezGALDevice::GetDefaultDevice()->GetPrimaryContext()->CopyTextureReadbackResult(GetPickingIdRT(), &SysMemDescs);
    }
  }
}

void ezPickingRenderPass::ReadBackProperties(ezView* pView)
{
  const ezUInt32 x = (ezUInt32)m_PickingPosition.x;
  const ezUInt32 y = (ezUInt32)m_PickingPosition.y;
  const ezUInt32 uiIndex = (y * m_uiWindowWidth) + x;

  if (uiIndex >= m_PickingResultsID.GetCount())
  {
    //ezLog::Error("Picking position %u, %u is outside the available picking area of %u * %u", x, y, m_uiWindowWidth, m_uiWindowHeight);
    return;
  }

  ezVec3 vNormal(0);
  ezVec3 vPickingRayStartPosition(0);
  ezVec3 vPickedPosition(0);
  {
    const float fDepth = m_PickingResultsDepth[uiIndex];
    ezGraphicsUtils::ConvertScreenPosToWorldPos(m_PickingInverseViewProjectionMatrix, 0, 0, m_uiWindowWidth, m_uiWindowHeight, ezVec3((float)x, (float)(m_uiWindowHeight - y), fDepth), vPickedPosition);
    ezGraphicsUtils::ConvertScreenPosToWorldPos(m_PickingInverseViewProjectionMatrix, 0, 0, m_uiWindowWidth, m_uiWindowHeight, ezVec3((float)x, (float)(m_uiWindowHeight - y), 0), vPickingRayStartPosition);

    float fOtherDepths[4] = { fDepth, fDepth, fDepth, fDepth };
    ezVec3 vOtherPos[4];
    ezVec3 vNormals[4];

    if ((ezUInt32)x + 1 < m_uiWindowWidth)
      fOtherDepths[0] = m_PickingResultsDepth[(y * m_uiWindowWidth) + x + 1];
    if (x > 0)
      fOtherDepths[1] = m_PickingResultsDepth[(y * m_uiWindowWidth) + x - 1];
    if ((ezUInt32)y + 1 < m_uiWindowHeight)
      fOtherDepths[2] = m_PickingResultsDepth[((y + 1) * m_uiWindowWidth) + x];
    if (y > 0)
      fOtherDepths[3] = m_PickingResultsDepth[((y - 1) * m_uiWindowWidth) + x];

    ezGraphicsUtils::ConvertScreenPosToWorldPos(m_PickingInverseViewProjectionMatrix, 0, 0, m_uiWindowWidth, m_uiWindowHeight, ezVec3((float)(x + 1), (float)(m_uiWindowHeight - y), fOtherDepths[0]), vOtherPos[0]);
    ezGraphicsUtils::ConvertScreenPosToWorldPos(m_PickingInverseViewProjectionMatrix, 0, 0, m_uiWindowWidth, m_uiWindowHeight, ezVec3((float)(x - 1), (float)(m_uiWindowHeight - y), fOtherDepths[1]), vOtherPos[1]);
    ezGraphicsUtils::ConvertScreenPosToWorldPos(m_PickingInverseViewProjectionMatrix, 0, 0, m_uiWindowWidth, m_uiWindowHeight, ezVec3((float)x, (float)(m_uiWindowHeight - (y + 1)), fOtherDepths[2]), vOtherPos[2]);
    ezGraphicsUtils::ConvertScreenPosToWorldPos(m_PickingInverseViewProjectionMatrix, 0, 0, m_uiWindowWidth, m_uiWindowHeight, ezVec3((float)x, (float)(m_uiWindowHeight - (y - 1)), fOtherDepths[3]), vOtherPos[3]);

    vNormals[0] = ezPlane(vPickedPosition, vOtherPos[0], vOtherPos[2]).m_vNormal;
    vNormals[1] = ezPlane(vPickedPosition, vOtherPos[2], vOtherPos[1]).m_vNormal;
    vNormals[2] = ezPlane(vPickedPosition, vOtherPos[1], vOtherPos[3]).m_vNormal;
    vNormals[3] = ezPlane(vPickedPosition, vOtherPos[3], vOtherPos[0]).m_vNormal;

    vNormal = (vNormals[0] + vNormals[1] + vNormals[2] + vNormals[3]).GetNormalized();
  }
  pView->SetRenderPassReadBackProperty(GetName(), "PickingMatrix", m_PickingInverseViewProjectionMatrix);
  pView->SetRenderPassReadBackProperty(GetName(), "PickingID", m_PickingResultsID[uiIndex]);
  pView->SetRenderPassReadBackProperty(GetName(), "PickingDepth", m_PickingResultsDepth[uiIndex]);
  pView->SetRenderPassReadBackProperty(GetName(), "PickingNormal", vNormal);
  pView->SetRenderPassReadBackProperty(GetName(), "PickingRayStartPosition", vPickingRayStartPosition);
  pView->SetRenderPassReadBackProperty(GetName(), "PickingPosition", vPickedPosition);
}

void ezPickingRenderPass::CreateTarget(const ezRectFloat& viewport)
{
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  // Create render target for picking
  ezGALTextureCreationDescription tcd;
  tcd.m_bAllowDynamicMipGeneration = false;
  tcd.m_bAllowShaderResourceView = false;
  tcd.m_bAllowUAV = false;
  tcd.m_bCreateRenderTarget = true;
  tcd.m_Format = ezGALResourceFormat::RGBAUByteNormalized;
  tcd.m_ResourceAccess.m_bReadBack = true;
  tcd.m_Type = ezGALTextureType::Texture2D;
  tcd.m_uiWidth = (ezUInt32)viewport.width;
  tcd.m_uiHeight = (ezUInt32)viewport.height;

  m_hPickingIdRT = pDevice->CreateTexture(tcd);

  tcd.m_Format = ezGALResourceFormat::DFloat;
  tcd.m_ResourceAccess.m_bReadBack = true;

  m_hPickingDepthRT = pDevice->CreateTexture(tcd);

  ezGALRenderTargetViewCreationDescription rtvd;
  rtvd.m_hTexture = m_hPickingIdRT;
  rtvd.m_RenderTargetType = ezGALRenderTargetType::Color;
  m_hPickingIdRTV = pDevice->CreateRenderTargetView(rtvd);

  rtvd.m_hTexture = m_hPickingDepthRT;
  rtvd.m_RenderTargetType = ezGALRenderTargetType::DepthStencil;
  m_hPickingDepthDSV = pDevice->CreateRenderTargetView(rtvd);

  m_RenderTargetSetup.SetRenderTarget(0, m_hPickingIdRTV)
    .SetDepthStencilTarget(m_hPickingDepthDSV);
}

void ezPickingRenderPass::DestroyTarget()
{
  ezGALDevice* pDevice = ezGALDevice::GetDefaultDevice();

  m_RenderTargetSetup.DestroyAllAttachedViews();
  if (!m_hPickingIdRT.IsInvalidated())
  {
    pDevice->DestroyTexture(m_hPickingIdRT);
    m_hPickingIdRT.Invalidate();
  }

  if (!m_hPickingDepthRT.IsInvalidated())
  {
    pDevice->DestroyTexture(m_hPickingDepthRT);
    m_hPickingDepthRT.Invalidate();
  }
}
