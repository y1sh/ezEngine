#include <PCH.h>
#include <ProceduralPlacementPlugin/Components/ProceduralPlacementComponent.h>
#include <ProceduralPlacementPlugin/Components/Implementation/ActiveTile.h>
#include <GameEngine/Interfaces/PhysicsWorldModule.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/Pipeline/ExtractedRenderData.h>
#include <RendererCore/Pipeline/View.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/Messages/UpdateLocalBoundsMessage.h>
#include <Foundation/Configuration/CVar.h>

namespace
{
  enum
  {
    MAX_TILE_INDEX = (1 << 20) - 1,
    TILE_INDEX_MASK = (1 << 21) - 1
  };

  EZ_ALWAYS_INLINE ezUInt64 GetTileKey(ezInt32 x, ezInt32 y, ezInt32 z)
  {
    ezUInt64 sx = (x + MAX_TILE_INDEX) & TILE_INDEX_MASK;
    ezUInt64 sy = (y + MAX_TILE_INDEX) & TILE_INDEX_MASK;
    ezUInt64 sz = (z + MAX_TILE_INDEX) & TILE_INDEX_MASK;

    return (sx << 42) | (sy << 21) | sz;
  }
}

using namespace ezPPInternal;

ezCVarFloat CVarCullDistanceScale("pp_CullDistanceScale", 1.0f, ezCVarFlags::Default, "Global scale to control cull distance for all layers");
ezCVarInt CVarMaxProcessingTiles("pp_MaxProcessingTiles", 10, ezCVarFlags::Default, "Maximum number of tiles in process");
ezCVarBool CVarVisTiles("pp_VisTiles", false, ezCVarFlags::Default, "Enables debug visualization of procedural placement tiles");

ezProceduralPlacementComponentManager::ezProceduralPlacementComponentManager(ezWorld* pWorld)
  : ezComponentManager<ezProceduralPlacementComponent, ezBlockStorageType::Compact>(pWorld)
{

}

ezProceduralPlacementComponentManager::~ezProceduralPlacementComponentManager()
{

}

void ezProceduralPlacementComponentManager::Initialize()
{
  {
    auto desc = EZ_CREATE_MODULE_UPDATE_FUNCTION_DESC(ezProceduralPlacementComponentManager::Update, this);
    desc.m_Phase = ezWorldModule::UpdateFunctionDesc::Phase::Async;

    this->RegisterUpdateFunction(desc);
  }

  {
    auto desc = EZ_CREATE_MODULE_UPDATE_FUNCTION_DESC(ezProceduralPlacementComponentManager::PlaceObjects, this);
    desc.m_Phase = ezWorldModule::UpdateFunctionDesc::Phase::PostAsync;

    this->RegisterUpdateFunction(desc);
  }

  ezResourceManager::s_ResourceEvents.AddEventHandler(ezMakeDelegate(&ezProceduralPlacementComponentManager::OnResourceEvent, this));
}

void ezProceduralPlacementComponentManager::Deinitialize()
{
  ezResourceManager::s_ResourceEvents.RemoveEventHandler(ezMakeDelegate(&ezProceduralPlacementComponentManager::OnResourceEvent, this));

  for (auto& activeTile : m_ActiveTiles)
  {
    activeTile.Deinitialize(*GetWorld());
  }
  m_ActiveTiles.Clear();
}

void ezProceduralPlacementComponentManager::Update(const ezWorldModule::UpdateContext& context)
{
  // Update resource data
  for (auto& hResource : m_ResourcesToUpdate)
  {
    ezUInt32 uiResourceIdHash = hResource.GetResourceIDHash();
    RemoveTilesForResource(uiResourceIdHash);

    ActiveResource& activeResource = m_ActiveResources[uiResourceIdHash];

    ezResourceLock<ezProceduralPlacementResource> pResource(hResource, ezResourceAcquireMode::NoFallback);
    auto layers = pResource->GetLayers();

    ezUInt32 uiLayerCount = layers.GetCount();
    activeResource.m_Layers.SetCount(uiLayerCount);
    for (ezUInt32 i = 0; i < uiLayerCount; ++i)
    {
      activeResource.m_Layers[i].m_pLayer = layers[i];
    }
  }
  m_ResourcesToUpdate.Clear();

  // TODO: split this function into tasks

  // Find new active tiles
  {
    ezHybridArray<ezSimdTransform, 8, ezAlignedAllocatorWrapper> localBoundingBoxes;

    for (auto& visibleResource : m_VisibleResources)
    {
      auto& hResource = visibleResource.m_hResource;
      ezUInt32 uiResourceIdHash = hResource.GetResourceIDHash();

      ActiveResource* pActiveResource = nullptr;
      EZ_VERIFY(m_ActiveResources.TryGetValue(uiResourceIdHash, pActiveResource), "Implementation error");

      auto& activeLayers = pActiveResource->m_Layers;

      for (ezUInt32 uiLayerIndex = 0; uiLayerIndex < activeLayers.GetCount(); ++uiLayerIndex)
      {
        auto& activeLayer = activeLayers[uiLayerIndex];

        const float fTileSize = activeLayer.m_pLayer->GetTileSize();
        const float fCullDistance = activeLayer.m_pLayer->m_fCullDistance * CVarCullDistanceScale;
        ezSimdVec4f fHalfTileSize = ezSimdVec4f(fTileSize * 0.5f);

        ezVec3 cameraPos = visibleResource.m_vCameraPosition / fTileSize;
        float fPosX = ezMath::Round(cameraPos.x);
        float fPosY = ezMath::Round(cameraPos.y);
        ezInt32 iPosX = static_cast<ezInt32>(fPosX);
        ezInt32 iPosY = static_cast<ezInt32>(fPosY);
        float fRadius = ezMath::Ceil(fCullDistance / fTileSize);
        ezInt32 iRadius = static_cast<ezInt32>(fRadius);
        ezInt32 iRadiusSqr = iRadius * iRadius;

        float fY = (fPosY - fRadius + 0.5f) * fTileSize;
        ezInt32 iY = -iRadius;

        while (iY <= iRadius)
        {
          float fX = (fPosX - fRadius + 0.5f) * fTileSize;
          ezInt32 iX = -iRadius;

          while (iX <= iRadius)
          {
            if (iX*iX + iY*iY <= iRadiusSqr)
            {
              ezSimdVec4f testPos = ezSimdVec4f(fX, fY, 0.0f);
              ezSimdFloat minZ = 10000.0f;
              ezSimdFloat maxZ = -10000.0f;

              localBoundingBoxes.Clear();

              for (auto& bounds : pActiveResource->m_Bounds)
              {
                ezSimdBBox extendedBox = bounds.m_GlobalBoundingBox;
                extendedBox.Grow(fHalfTileSize);

                if (((testPos >= extendedBox.m_Min) && (testPos <= extendedBox.m_Max)).AllSet<2>())
                {
                  minZ = minZ.Min(bounds.m_GlobalBoundingBox.m_Min.z());
                  maxZ = maxZ.Max(bounds.m_GlobalBoundingBox.m_Max.z());

                  localBoundingBoxes.PushBack(bounds.m_LocalBoundingBox);
                }
              }

              if (!localBoundingBoxes.IsEmpty())
              {
                ezUInt64 uiTileKey = GetTileKey(iPosX + iX, iPosY + iY, 0);
                if (!activeLayer.m_TileIndices.Contains(uiTileKey))
                {
                  activeLayer.m_TileIndices.Insert(uiTileKey, ezInvalidIndex);

                  auto& newTile = m_NewTiles.ExpandAndGetRef();
                  newTile.m_uiResourceIdHash = uiResourceIdHash;
                  newTile.m_uiLayerIndex = uiLayerIndex;
                  newTile.m_iPosX = iPosX + iX;
                  newTile.m_iPosY = iPosY + iY;
                  newTile.m_fMinZ = minZ;
                  newTile.m_fMaxZ = maxZ;
                  newTile.m_LocalBoundingBoxes = localBoundingBoxes;
                }
              }
            }

            ++iX;
            fX += fTileSize;
          }

          ++iY;
          fY += fTileSize;
        }
      }
    }

    ClearVisibleResources();
  }

  // Allocate new tiles
  {
    while (!m_NewTiles.IsEmpty() && m_ProcessingTiles.GetCount() < (ezUInt32)CVarMaxProcessingTiles)
    {
      const TileDesc& newTile = m_NewTiles.PeekBack();
      auto& pLayer = m_ActiveResources[newTile.m_uiResourceIdHash].m_Layers[newTile.m_uiLayerIndex].m_pLayer;
      ezUInt32 uiNewTileIndex = AllocateTile(newTile, pLayer);

      m_ProcessingTiles.PushBack(uiNewTileIndex);
      m_NewTiles.PopBack();
    }
  }

  const ezWorld* pWorld = GetWorld();

  // Update processing tiles
  if (GetWorldSimulationEnabled())
  {
    if (const ezPhysicsWorldModuleInterface* pPhysicsModule = pWorld->GetModule<ezPhysicsWorldModuleInterface>())
    {
      for (auto& uiTileIndex : m_ProcessingTiles)
      {
        m_ActiveTiles[uiTileIndex].Update(pPhysicsModule);
      }
    }
  }

  // Debug draw tiles
  if (CVarVisTiles)
  {
    for (auto& activeTile : m_ActiveTiles)
    {
      if (!activeTile.IsValid())
        continue;

      ezBoundingBox bbox = activeTile.GetBoundingBox();
      ezColor color = activeTile.GetDebugColor();

      ezDebugRenderer::DrawLineBox(pWorld, bbox, color);
    }
  }
}

void ezProceduralPlacementComponentManager::PlaceObjects(const ezWorldModule::UpdateContext& context)
{
  for (ezUInt32 i = 0; i < m_ProcessingTiles.GetCount(); ++i)
  {
    ezUInt32 uiTileIndex = m_ProcessingTiles[i];

    auto& activeTile = m_ActiveTiles[uiTileIndex];

    if (activeTile.IsFinished())
    {
      if (activeTile.PlaceObjects(*GetWorld()) > 0)
      {
        auto& tileDesc = activeTile.GetDesc();

        auto& activeLayer = m_ActiveResources[tileDesc.m_uiResourceIdHash].m_Layers[tileDesc.m_uiLayerIndex];

        ezUInt64 uiTileKey = GetTileKey(tileDesc.m_iPosX, tileDesc.m_iPosY, 0);
        activeLayer.m_TileIndices[uiTileKey] = uiTileIndex;
      }
      else
      {
        // mark tile for re-use
        DeallocateTile(uiTileIndex);
      }

      m_ProcessingTiles.RemoveAtSwap(i);
      --i;
    }
  }
}

void ezProceduralPlacementComponentManager::AddComponent(ezProceduralPlacementComponent* pComponent)
{
  auto& hResource = pComponent->GetResource();
  if (!hResource.IsValid())
  {
    return;
  }

  if (!m_ResourcesToUpdate.Contains(hResource))
  {
    m_ResourcesToUpdate.PushBack(hResource);
  }

  ezSimdTransform localBoundingBox = pComponent->GetOwner()->GetGlobalTransformSimd();
  localBoundingBox.m_Scale = localBoundingBox.m_Scale.CompMul(ezSimdConversion::ToVec3(pComponent->GetExtents() * 0.5f));
  localBoundingBox.Invert();

  ezUInt32 uiResourceIdHash = hResource.GetResourceIDHash();
  ActiveResource& activeResource = m_ActiveResources[uiResourceIdHash];

  auto& bounds = activeResource.m_Bounds.ExpandAndGetRef();
  bounds.m_GlobalBoundingBox = pComponent->GetOwner()->GetGlobalBoundsSimd().GetBox();
  bounds.m_LocalBoundingBox = localBoundingBox;
  bounds.m_hComponent = pComponent->GetHandle();
}

void ezProceduralPlacementComponentManager::RemoveComponent(ezProceduralPlacementComponent* pComponent)
{
  auto& hResource = pComponent->GetResource();
  if (!hResource.IsValid())
  {
    return;
  }

  ezUInt32 uiResourceIdHash = hResource.GetResourceIDHash();

  ActiveResource* pActiveResource = nullptr;
  if (m_ActiveResources.TryGetValue(uiResourceIdHash, pActiveResource))
  {
    ezComponentHandle hComponent = pComponent->GetHandle();

    for (ezUInt32 i = 0; i < pActiveResource->m_Bounds.GetCount(); ++i)
    {
      auto& bounds = pActiveResource->m_Bounds[i];
      if (bounds.m_hComponent == hComponent)
      {
        pActiveResource->m_Bounds.RemoveAtSwap(i);
        break;
      }
    }
  }

  RemoveTilesForResource(uiResourceIdHash);
}

ezUInt32 ezProceduralPlacementComponentManager::AllocateTile(const TileDesc& desc, ezSharedPtr<const Layer>& pLayer)
{
  ezUInt32 uiNewTileIndex = ezInvalidIndex;
  if (!m_FreeTiles.IsEmpty())
  {
    uiNewTileIndex = m_FreeTiles.PeekBack();
    m_FreeTiles.PopBack();
  }
  else
  {
    uiNewTileIndex = m_ActiveTiles.GetCount();
    m_ActiveTiles.ExpandAndGetRef();
  }

  m_ActiveTiles[uiNewTileIndex].Initialize(desc, pLayer);
  return uiNewTileIndex;
}

void ezProceduralPlacementComponentManager::DeallocateTile(ezUInt32 uiTileIndex)
{
  m_ActiveTiles[uiTileIndex].Deinitialize(*GetWorld());
  m_FreeTiles.PushBack(uiTileIndex);
}

void ezProceduralPlacementComponentManager::RemoveTilesForResource(ezUInt32 uiResourceIdHash)
{
  ActiveResource* pActiveResource = nullptr;
  if (!m_ActiveResources.TryGetValue(uiResourceIdHash, pActiveResource))
    return;

  for (auto& layer : pActiveResource->m_Layers)
  {
    layer.m_TileIndices.Clear();
  }

  for (ezUInt32 uiNewTileIndex = 0; uiNewTileIndex < m_NewTiles.GetCount(); ++uiNewTileIndex)
  {
    if (m_NewTiles[uiNewTileIndex].m_uiResourceIdHash == uiResourceIdHash)
    {
      m_NewTiles.RemoveAtSwap(uiNewTileIndex);
      --uiNewTileIndex;
    }
  }

  for (ezUInt32 uiTileIndex = 0; uiTileIndex < m_ActiveTiles.GetCount(); ++uiTileIndex)
  {
    auto& activeTile = m_ActiveTiles[uiTileIndex];
    if (!activeTile.IsValid())
      continue;

    auto& tileDesc = activeTile.GetDesc();
    if (tileDesc.m_uiResourceIdHash == uiResourceIdHash)
    {
      DeallocateTile(uiTileIndex);

      m_ProcessingTiles.Remove(uiTileIndex);
    }
  }
}

void ezProceduralPlacementComponentManager::OnResourceEvent(const ezResourceEvent& resourceEvent)
{
  if (resourceEvent.m_EventType != ezResourceEventType::ResourceContentUnloading)
    return;

  if (auto pResource = ezDynamicCast<const ezProceduralPlacementResource*>(resourceEvent.m_pResource))
  {
    ezProceduralPlacementResourceHandle hResource = pResource->GetHandle();

    if (!m_ResourcesToUpdate.Contains(hResource))
    {
      m_ResourcesToUpdate.PushBack(hResource);
    }
  }
}

void ezProceduralPlacementComponentManager::AddVisibleResource(const ezProceduralPlacementResourceHandle& hResource, const ezVec3& cameraPosition,
  const ezVec3& cameraDirection) const
{
  EZ_LOCK(m_VisibleResourcesMutex);

  for (auto& visibleResource : m_VisibleResources)
  {
    if (visibleResource.m_hResource == hResource &&
      visibleResource.m_vCameraPosition == cameraPosition &&
      visibleResource.m_vCameraDirection == cameraDirection)
    {
      return;
    }
  }

  auto& visibleResource = m_VisibleResources.ExpandAndGetRef();
  visibleResource.m_hResource = hResource;
  visibleResource.m_vCameraPosition = cameraPosition;
  visibleResource.m_vCameraDirection = cameraDirection;
}

void ezProceduralPlacementComponentManager::ClearVisibleResources()
{
  m_VisibleResources.Clear();
}

//////////////////////////////////////////////////////////////////////////

EZ_BEGIN_COMPONENT_TYPE(ezProceduralPlacementComponent, 1, ezComponentMode::Static)
{
  EZ_BEGIN_PROPERTIES
  {
    EZ_ACCESSOR_PROPERTY("Resource", GetResourceFile, SetResourceFile)->AddAttributes(new ezAssetBrowserAttribute("Procedural Placement")),
    EZ_ACCESSOR_PROPERTY("Extents", GetExtents, SetExtents)->AddAttributes(new ezDefaultValueAttribute(ezVec3(10.0f)), new ezClampValueAttribute(ezVec3(0), ezVariant())),
  }
  EZ_END_PROPERTIES
  EZ_BEGIN_MESSAGEHANDLERS
  {
    EZ_MESSAGE_HANDLER(ezUpdateLocalBoundsMessage, OnUpdateLocalBounds),
    EZ_MESSAGE_HANDLER(ezExtractRenderDataMessage, OnExtractRenderData),
  }
  EZ_END_MESSAGEHANDLERS
  EZ_BEGIN_ATTRIBUTES
  {
    new ezCategoryAttribute("ProceduralPlacement"),
    new ezBoxManipulatorAttribute("Extents"),
    new ezBoxVisualizerAttribute("Extents"),
  }
  EZ_END_ATTRIBUTES
}
EZ_END_COMPONENT_TYPE

ezProceduralPlacementComponent::ezProceduralPlacementComponent()
{
  m_vExtents.Set(10.0f);
}

ezProceduralPlacementComponent::~ezProceduralPlacementComponent()
{

}

void ezProceduralPlacementComponent::OnActivated()
{
  GetOwner()->UpdateLocalBounds();
  GetManager()->AddComponent(this);
}

void ezProceduralPlacementComponent::OnDeactivated()
{
  GetOwner()->UpdateLocalBounds();
  GetManager()->RemoveComponent(this);
}

void ezProceduralPlacementComponent::SetResourceFile(const char* szFile)
{
  ezProceduralPlacementResourceHandle hResource;

  if (!ezStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = ezResourceManager::LoadResource<ezProceduralPlacementResource>(szFile, ezResourcePriority::High, ezProceduralPlacementResourceHandle());
    ezResourceManager::PreloadResource(hResource, ezTime::Seconds(0.0));
  }

  SetResource(hResource);
}

const char* ezProceduralPlacementComponent::GetResourceFile() const
{
  if (!m_hResource.IsValid())
    return "";

  return m_hResource.GetResourceID();
}

void ezProceduralPlacementComponent::SetResource(const ezProceduralPlacementResourceHandle& hResource)
{
  if (IsActiveAndInitialized())
  {
    GetManager()->RemoveComponent(this);
  }

  m_hResource = hResource;

  if (IsActiveAndInitialized())
  {
    GetManager()->AddComponent(this);
  }
}

void ezProceduralPlacementComponent::SetExtents(const ezVec3& value)
{
  m_vExtents = value.CompMax(ezVec3::ZeroVector());

  if (IsActiveAndInitialized())
  {
    GetManager()->RemoveComponent(this);

    GetOwner()->UpdateLocalBounds();

    GetManager()->AddComponent(this);
  }
}

void ezProceduralPlacementComponent::OnUpdateLocalBounds(ezUpdateLocalBoundsMessage& msg)
{
  msg.AddBounds(ezBoundingBox(-m_vExtents * 0.5f, m_vExtents * 0.5f));
}

void ezProceduralPlacementComponent::OnExtractRenderData(ezExtractRenderDataMessage& msg) const
{
  // Don't extract render data for selection or in shadow views.
  if (msg.m_OverrideCategory != ezInvalidIndex)
    return;

  if (msg.m_pView->GetCameraUsageHint() == ezCameraUsageHint::MainView ||
    msg.m_pView->GetCameraUsageHint() == ezCameraUsageHint::EditorView)
  {
    const ezCamera* pCamera = msg.m_pView->GetCullingCamera();

    ezVec3 cameraPosition = pCamera->GetCenterPosition();
    ezVec3 cameraDirection = pCamera->GetCenterDirForwards();

    if (m_hResource.IsValid())
    {
      GetManager()->AddVisibleResource(m_hResource, cameraPosition, cameraDirection);
    }
  }
}

void ezProceduralPlacementComponent::SerializeComponent(ezWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);

  ezStreamWriter& s = stream.GetStream();

  s << m_hResource;
  s << m_vExtents;
}

void ezProceduralPlacementComponent::DeserializeComponent(ezWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  //const ezUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  ezStreamReader& s = stream.GetStream();

  s >> m_hResource;
  s >> m_vExtents;
}
