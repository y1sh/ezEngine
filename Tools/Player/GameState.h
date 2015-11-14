#pragma once

#include <CoreUtils/Graphics/Camera.h>
#include <RendererCore/Pipeline/View.h>
#include <GameFoundation/GameApplication.h>
#include <Core/Scene/Scene.h>

class GameWindow;

class GameState : public ezGameState
{
public:
  GameState();

private:
  virtual void Activate() override;
  virtual void Deactivate() override;
  virtual void BeforeWorldUpdate() override;
  
  void UpdateInputSystem(ezTime UpdateDiff);
  void UpdateGameInput();
  
  void SetupInput();
  void CreateGameLevelAndRenderPipeline(ezGALRenderTargetViewHandle hBackBuffer, ezGALRenderTargetViewHandle hDSV, const char* szLevelFile);
  void DestroyGameLevel();

  GameWindow* m_pWindow;

  ezRenderPipeline* m_pRenderPipeline;

  ezScene* m_pScene;
  ezWorld* m_pWorld;
  ezCamera m_Camera;

  ezView* m_pView;
};