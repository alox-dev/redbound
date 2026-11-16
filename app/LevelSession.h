#pragma once
#include "SceneTarget.h"
#include "AmbientSounds.h"
#include "FadeOverlay.h"
#include "Hud.h"
#include "LaunchOptions.h"
#include "LevelCore.h"
#include "LevelMenu.h"
#include "PlayerInput.h"
#include "SkyRenderer.h"
#include <SDL.h>
#include <lang/Object.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gr { class GLES2_Context; }
namespace hgr { class DefaultResourceManager; class Camera; }

namespace redbound {

struct AppContext;
class Audio;
class Ui;
class WaterReflection;

struct SessionResult {
	bool quit = false;
	std::vector<std::string> next;
};

class LevelSession {
public:
	LevelSession(AppContext& app, const LaunchOptions& options);
	~LevelSession();
	SessionResult run();
private:
	AppContext& m_app;
	LaunchOptions m_options;
	P(gr::GLES2_Context) m_ctx;
	P(hgr::DefaultResourceManager) m_resources;
	std::unique_ptr<LevelCore> m_core;
	std::unique_ptr<Ui> m_ui;
	bool m_hasUi = false;
	std::unique_ptr<Audio> m_audio;
	AmbientSounds m_ambient;
	std::unique_ptr<WaterReflection> m_water;
	SkyRenderer m_sky;
	FadeOverlay m_fade;
	Hud m_hud;
	LevelMenu m_menu;
	PlayerInput m_input;
	SessionResult m_result;
	bool m_running = true;
	bool m_results = false;
	bool m_needCapture = false;
	bool m_mouseLook = false;
	int m_mouseTravel = 0;
	int m_stickNav = 0;
	bool m_orientationLocked = false;
	SceneTarget m_sceneTarget;
	float m_fps = 0.f, m_fpsTime = 0.f;
	int m_fpsFrames = 0;
	float m_mouseYaw = 0.f, m_mousePitch = 0.f;
	Uint32 m_touchPauseAt = 0;
	int m_frame = 0;

	void showLoadingScreen();
	void loadScene();
	void setupWorld();
	void setupPlayer();
	void handleEvents();
	bool handlePauseEvent(const SDL_Event& e);
	void pauseForBackground();
	bool mouseCaptureAllowed() const;
	void setMouseCapture(bool capture);
	void handleMenuAction(MenuAction action);
	void handleDevKey(SDL_Keycode key);
	void update(float dt, float realDt);
	LevelInput readInput(float dt);
	void render(float realDt, bool paused, bool liveBlur);
	void requestLevel(const std::vector<std::string>& args);
};

}
