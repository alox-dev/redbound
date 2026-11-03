#pragma once
#include "AmbientSounds.h"
#include "FadeOverlay.h"
#include "Hud.h"
#include "LaunchOptions.h"
#include "LevelMenu.h"
#include "PlayerInput.h"
#include "SceneSetup.h"
#include "SkyRenderer.h"
#include <SDL.h>
#include <lang/Object.h>
#include <math/float3.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gr { class GLES2_Context; }
namespace hgr { class DefaultResourceManager; class Scene; class Camera; class Dummy; class ParticleSystem; }

namespace redbound {

struct AppContext;
class Audio;
class Ui;
class SceneWorld;
class BallController;
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
	std::unique_ptr<Ui> m_ui;
	bool m_hasUi = false;
	P(hgr::Scene) m_scene;
	std::vector<LibraryObject> m_libraryObjects;
	std::vector<hgr::Node*> m_helpers;
	std::vector<hgr::Camera*> m_cameras;
	std::unique_ptr<Audio> m_audio;
	AmbientSounds m_ambient;
	std::unique_ptr<SceneWorld> m_world;
	std::unique_ptr<BallController> m_player;
	P(hgr::Dummy) m_ballRoot;
	std::unique_ptr<WaterReflection> m_water;
	P(hgr::Camera) m_camera;
	SkyRenderer m_sky;
	FadeOverlay m_fade;
	Hud m_hud;
	LevelMenu m_menu;
	PlayerInput m_input;
	std::vector<std::pair<P(hgr::ParticleSystem), float>> m_particles;
	std::string m_boostParticles;
	unsigned m_clearColor = 0xFF6688AAu;
	SessionResult m_result;
	bool m_running = true;
	bool m_results = false;
	bool m_needCapture = false;
	float m_completeTimer = 0.f;
	bool m_boostWas = false;
	bool m_mouseLook = false;
	bool m_freeFly = false;
	bool m_followGameCamera = true;
	bool m_helpersVisible = false;
	int m_devCamera = -1;
	float m_yaw = 0.f, m_pitch = 0.f;
	math::float3 m_position;
	std::string m_lastRunning;
	Uint32 m_touchPauseAt = 0;
	int m_frame = 0;

	void showLoadingScreen();
	void loadScene();
	void setupWorld();
	void setupPlayer();
	void applyModelPose(hgr::Scene* ballScene);
	void setupCamera();
	void handleEvents();
	bool handlePauseEvent(const SDL_Event& e);
	void handleMenuAction(MenuAction action);
	void handleDevKey(SDL_Keycode key);
	void update(float dt, float realDt);
	void updateWorld(float dt);
	void updatePlayer(float dt);
	void updateFreeCamera(float dt);
	void spawnBoostParticles();
	hgr::Camera* activeCamera();
	void render(float realDt, bool paused, bool liveBlur);
	void requestLevel(const std::vector<std::string>& args);
};

}
