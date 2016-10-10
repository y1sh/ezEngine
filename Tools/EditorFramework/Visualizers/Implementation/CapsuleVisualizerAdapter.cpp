#include <PCH.h>
#include <EditorFramework/Visualizers/CapsuleVisualizerAdapter.h>
#include <EditorFramework/Assets/AssetDocument.h>
#include <EditorFramework/Gizmos/GizmoHandle.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

ezCapsuleVisualizerAdapter::ezCapsuleVisualizerAdapter()
{
}

ezCapsuleVisualizerAdapter::~ezCapsuleVisualizerAdapter()
{
}

void ezCapsuleVisualizerAdapter::Finalize()
{
  auto* pDoc = m_pObject->GetDocumentObjectManager()->GetDocument();
  const ezAssetDocument* pAssetDocument = ezDynamicCast<const ezAssetDocument*>(pDoc);
  EZ_ASSERT_DEV(pAssetDocument != nullptr, "Visualizers are only supported in ezAssetDocument.");

  const ezCapsuleVisualizerAttribute* pAttr = static_cast<const ezCapsuleVisualizerAttribute*>(m_pVisualizerAttr);

  m_Cylinder.Configure(nullptr, ezEngineGizmoHandleType::CylinderZ, pAttr->m_Color, false, false, true);
  m_SphereTop.Configure(nullptr, ezEngineGizmoHandleType::HalfSphereZ, pAttr->m_Color, false, false, true);
  m_SphereBottom.Configure(nullptr, ezEngineGizmoHandleType::HalfSphereZ, pAttr->m_Color, false, false, true);


  m_Cylinder.SetOwner(pAssetDocument);
  m_SphereTop.SetOwner(pAssetDocument);
  m_SphereBottom.SetOwner(pAssetDocument);

  m_Cylinder.SetVisible(m_bVisualizerIsVisible);
  m_SphereTop.SetVisible(m_bVisualizerIsVisible);
  m_SphereBottom.SetVisible(m_bVisualizerIsVisible);
}

void ezCapsuleVisualizerAdapter::Update()
{
  const ezCapsuleVisualizerAttribute* pAttr = static_cast<const ezCapsuleVisualizerAttribute*>(m_pVisualizerAttr);
  ezObjectAccessorBase* pObjectAccessor = GetObjectAccessor();
  m_Cylinder.SetVisible(m_bVisualizerIsVisible);
  m_SphereTop.SetVisible(m_bVisualizerIsVisible);
  m_SphereBottom.SetVisible(m_bVisualizerIsVisible);

  float fRadius = 1.0f;
  float fHeight = 0.0f;

  if (!pAttr->GetRadiusProperty().IsEmpty())
  {
    ezVariant value;
    pObjectAccessor->GetValue(m_pObject, GetProperty(pAttr->GetRadiusProperty()), value);

    EZ_ASSERT_DEBUG(value.IsValid() && value.CanConvertTo<float>(), "Invalid property bound to ezCapsuleVisualizerAttribute 'radius'");
    fRadius = value.ConvertTo<float>();
  }

  if (!pAttr->GetHeightProperty().IsEmpty())
  {
    ezVariant value;
    pObjectAccessor->GetValue(m_pObject, GetProperty(pAttr->GetHeightProperty()), value);

    EZ_ASSERT_DEBUG(value.IsValid() && value.CanConvertTo<float>(), "Invalid property bound to ezCapsuleVisualizerAttribute 'height'");
    fHeight = value.ConvertTo<float>();
  }

  if (!pAttr->GetColorProperty().IsEmpty())
  {
    ezVariant value;
    pObjectAccessor->GetValue(m_pObject, GetProperty(pAttr->GetColorProperty()), value);

    EZ_ASSERT_DEBUG(value.IsValid() && value.CanConvertTo<ezColor>(), "Invalid property bound to ezCapsuleVisualizerAttribute 'color'");
    m_SphereTop.SetColor(value.ConvertTo<ezColor>());
    m_SphereBottom.SetColor(value.ConvertTo<ezColor>());
    m_Cylinder.SetColor(value.ConvertTo<ezColor>());
  }

  m_ScaleCylinder.SetScalingMatrix(ezVec3(fRadius, fRadius, fHeight));

  m_ScaleSphereTop.SetScalingMatrix(ezVec3(fRadius));
  m_ScaleSphereTop.SetTranslationVector(ezVec3(0, 0, fHeight * 0.5f));

  m_ScaleSphereBottom.SetScalingMatrix(ezVec3(fRadius, -fRadius, -fRadius));
  m_ScaleSphereBottom.SetTranslationVector(ezVec3(0, 0, -fHeight * 0.5f));

}

void ezCapsuleVisualizerAdapter::UpdateGizmoTransform()
{
  m_SphereTop.SetTransformation(GetObjectTransform().GetAsMat4() * m_ScaleSphereTop);
  m_SphereBottom.SetTransformation(GetObjectTransform().GetAsMat4() * m_ScaleSphereBottom);
  m_Cylinder.SetTransformation(GetObjectTransform().GetAsMat4() * m_ScaleCylinder);
}


