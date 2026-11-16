#pragma once
#include "LaunchOptions.h"
#include "PlayerCommand.h"
#include "SceneSetup.h"
#include <lang/Object.h>
#include <math/float3.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gr { class Context; }
namespace hgr { class DefaultResourceManager; class Scene; class Camera; class Dummy; class Node; class ParticleSystem; }

namespace redbound {

class SceneWorld;
class BallController;

struct FreeFlyInput {
	float yaw = 0.f;
	float pitch = 0.f;
	float forward = 0.f;
	float right = 0.f;
	float up = 0.f;
	bool fast = false;
};

struct LevelInput {
	PlayerCommand command;
	FreeFlyInput fly;
	bool tap = false;
	bool guidedCamera = false;
};

struct LevelFog {
	bool enabled = false;
	float start = 0.f, end = 0.f;
	float color[3] = { 0.f, 0.f, 0.f };
};

class LevelCore {
public:
	LevelCore(gr::Context* ctx, hgr::DefaultResourceManager* resources, const LaunchOptions& options);
	~LevelCore();

	void loadScene();
	void setupWorld();
	void setupPlayer();
	void setupCamera();
	bool update(float dt, float realDt, const LevelInput& input, bool resultsAllowed);
	hgr::Camera* activeCamera();

	void cycleDevCamera();
	void toggleHelpers();
	void toggleFreeFly();
	void respawn();

	hgr::Scene* scene() const { return m_scene; }
	SceneWorld& world() const { return *m_world; }
	BallController* player() const { return m_player.get(); }
	hgr::Camera* camera() const { return m_camera; }
	const std::vector<hgr::Camera*>& cameras() const { return m_cameras; }
	hgr::Node* skyNode() const { return m_skyNode; }
	unsigned clearColor() const { return m_clearColor; }
	const LevelFog& fog() const { return m_fog; }
	int collectableTotal() const { return m_collectables; }
	bool followGameCamera() const { return m_followGameCamera; }
	bool freeFly() const { return m_freeFly; }
	int devCamera() const { return m_devCamera; }
	math::float3 listenerPosition() const;

	std::function<void(const std::string& file, bool loop)> playSound;
	std::function<void(const std::string& file)> stopSound;
	std::function<void(const std::string& file, float volume)> playPlayerSound;
	std::function<void(float strength)> onPressure;

private:
	P(gr::Context) m_ctx;
	P(hgr::DefaultResourceManager) m_resources;
	LaunchOptions m_options;
	P(hgr::Scene) m_scene;
	std::vector<LibraryObject> m_libraryObjects;
	std::vector<hgr::Node*> m_helpers;
	std::vector<hgr::Camera*> m_cameras;
	std::unique_ptr<SceneWorld> m_world;
	std::unique_ptr<BallController> m_player;
	P(hgr::Dummy) m_ballRoot;
	P(hgr::Scene) m_ballScene;
	P(hgr::Camera) m_camera;
	hgr::Node* m_skyNode = 0;
	std::vector<std::pair<P(hgr::ParticleSystem), float>> m_particles;
	std::string m_boostParticles;
	unsigned m_clearColor = 0xFF6688AAu;
	LevelFog m_fog;
	int m_collectables = 0;
	float m_completeTimer = 0.f;
	bool m_resultsShown = false;
	bool m_boostWas = false;
	bool m_freeFly = false;
	bool m_followGameCamera = true;
	bool m_helpersVisible = false;
	int m_devCamera = -1;
	float m_yaw = 0.f, m_pitch = 0.f;
	float m_baseFov = -1.f;
	math::float3 m_position;
	std::string m_lastRunning;

	void buildPlayer();
	void applyModelPose(hgr::Scene* ballScene);
	void updateWorld(float dt, bool tap);
	void updatePlayer(float dt, const LevelInput& input);
	void updateFreeCamera(float dt, const FreeFlyInput& fly);
	void spawnBoostParticles();
};

}
