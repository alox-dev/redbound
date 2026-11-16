#include "LevelCore.h"
#include "BallController.h"
#include "FileUtils.h"
#include "SceneWorld.h"
#include <gr/Context.h>
#include <hgr/Camera.h>
#include <hgr/DefaultResourceManager.h>
#include <hgr/Dummy.h>
#include <hgr/Mesh.h>
#include <hgr/ParticleSystem.h>
#include <hgr/Scene.h>
#include <hgr/TransformAnimation.h>
#include <hgr/TransformAnimationSet.h>
#include <hgr/UserPropertySet.h>
#include <lang/Hashtable.h>
#include <lang/Throwable.h>
#include <math/float3x3.h>
#include <math/quaternion.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>

namespace redbound {

static bool traceEnabled() { return getenv("REDBOUND_TRACE") != 0; }

LevelCore::LevelCore(gr::Context* ctx, hgr::DefaultResourceManager* resources, const LaunchOptions& options) :
	m_ctx(ctx), m_resources(resources), m_options(options), m_position(0, 200, -600) {}

LevelCore::~LevelCore() {}

void LevelCore::loadScene() {
	m_scene = new hgr::Scene(m_ctx, m_options.levelFile.c_str(), m_resources, m_options.textureDir().c_str(), m_options.shaderDir().c_str(),
	                         m_options.particleDir().c_str());
	loadLibraryObjects(m_scene, m_ctx, m_resources, m_options, m_libraryObjects);
	m_fog.enabled = linearFog(m_scene, m_fog.start, m_fog.end, m_fog.color);
	m_helpers = hideHelperNodes(m_scene);
	m_cameras = collectCameras(m_scene);
	m_clearColor = backgroundColor(m_scene);
}

void LevelCore::setupWorld() {
	m_world.reset(new SceneWorld(m_scene));
	if (m_options.menu) m_world->setVariable("attraction", 1.f);
	for (const auto& v : m_options.variables) m_world->setVariable(v.first, v.second);
	m_collectables = 0;
	for (LibraryObject& object : m_libraryObjects) {
		if (object.properties.find("a01") == std::string::npos) continue;
		m_world->registerNode(object.marker, object.visual);
		std::string props = object.properties;
		for (size_t at = props.find(",this"); at != std::string::npos; at = props.find(",this", at + 1)) {
			const char next = at + 5 < props.size() ? props[at + 5] : '\n';
			if (next == '\n' || next == '\r' || next == ',') props.replace(at + 1, 4, object.marker);
		}
		m_world->addTrigger(object.trigger, props);
		if (props.find("item=collectable") != std::string::npos) ++m_collectables;
	}
	m_world->spawnParticles = [this](const std::string& file, hgr::Node* at) {
		try {
			hgr::ParticleSystem* source = m_resources->getParticleSystem((m_options.particleDir() + file).c_str(), m_options.textureDir().c_str(),
			                                                             m_options.shaderDir().c_str());
			if (!source) return;
			P(hgr::ParticleSystem) ps = new hgr::ParticleSystem(*source);
			ps->setTransform(at->worldTransform());
			ps->linkTo(m_scene);
			m_particles.push_back(std::make_pair(ps, 8.f));
		} catch (lang::Throwable& e) {
			fprintf(stderr, "particles '%s': %s\n", file.c_str(), e.getMessage().format().c_str());
		}
	};
	m_world->playSound = [this](const std::string& file, bool loop) { if (playSound) playSound(file, loop); };
	m_world->stopSound = [this](const std::string& file) { if (stopSound) stopSound(file); };
	m_world->onPressure = [this](float strength) { if (onPressure) onPressure(strength); };
	printf("logic: %d triggers, %d warnings\n", (int)m_world->triggerCount(), (int)m_world->warnings().size());
	for (size_t i = 0; i < m_world->warnings().size() && i < 10; ++i) printf("  warning: %s\n", m_world->warnings()[i].c_str());
}

void LevelCore::applyModelPose(hgr::Scene* ballScene) {
	hgr::TransformAnimationSet* animations = ballScene->transformAnimations();
	hgr::Node* eyes = m_ballRoot->findNodeByNameFromSubtree("G_bounce_lookat_eyes");
	if (!animations || !eyes) return;
	for (lang::HashtableIterator<lang::String, P(hgr::TransformAnimation)> it = animations->begin(); it != animations->end(); ++it) {
		hgr::Node* node = m_ballRoot->findNodeByNameFromSubtree(it.key().c_str());
		if (!node || !it.value()) continue;
		bool insideEyes = false;
		for (hgr::Node* q = node; q && !insideEyes; q = q->parent()) insideEyes = q == eyes;
		if (!insideEyes) continue;
		math::float3 position, scale;
		math::quaternion rotation;
		it.value()->eval(0.f, &position, &rotation, &scale);
		math::float3x4 tm(math::float3x3(rotation), position);
		for (int r = 0; r < 3; ++r) {
			tm(r, 0) *= scale.x;
			tm(r, 1) *= scale.y;
			tm(r, 2) *= scale.z;
		}
		node->setTransform(tm);
	}
}

void LevelCore::setupPlayer() {
	try {
		buildPlayer();
	} catch (...) {
		m_player.reset();
		throw;
	}
}

void LevelCore::buildPlayer() {
	P(hgr::Scene) ballScene = new hgr::Scene(m_ctx, (m_options.dataDir + "/resources/models/bounce.hgr").c_str(), m_resources,
	                                         m_options.textureDir().c_str(), m_options.shaderDir().c_str(), m_options.particleDir().c_str());
	m_ballRoot = new hgr::Dummy;
	m_ballRoot->setName("player");
	std::vector<P(hgr::Node)> children;
	for (hgr::Node* c = ballScene->firstChild(); c; c = c->next(ballScene))
		if (c->parent() == (hgr::Node*)ballScene) children.push_back(c);
	for (auto& c : children) c->linkTo(m_ballRoot);
	m_ballRoot->linkTo(m_scene);
	m_player.reset(new BallController(m_scene, m_world.get()));
	for (LibraryObject& object : m_libraryObjects) {
		const bool noShadow = object.properties.find("shadow=0") != std::string::npos;
		std::vector<hgr::Node*> shadows;
		for (hgr::Node* c = object.visual; c; c = c->next(object.visual))
			if (c->classId() == hgr::Node::NODE_MESH && toLower(c->name().c_str()).find("shadow") != std::string::npos) shadows.push_back(c);
		for (hgr::Node* s : shadows) m_player->addObjectShadow(s, object.visual, noShadow);
		if (object.properties.find("a01") != std::string::npos) {
			const size_t at = object.properties.find("radius=");
			const float radius = at != std::string::npos ? (float)atof(object.properties.c_str() + at + 7) : 60.f;
			m_player->addSphereTrigger(object.trigger, object.visual, radius);
		}
	}
	if (hgr::UserPropertySet* props = ballScene->userProperties())
		if (props->containsKey("D_forminfo")) {
			const std::string text = props->get("D_forminfo").c_str();
			const size_t eq = text.find('=');
			if (eq != std::string::npos) {
				std::string v = text.substr(eq + 1);
				m_player->setFormInfo(v.substr(0, v.find_first_of("\r\n")));
			}
		}
	m_ballScene = ballScene;
	m_world->addAnimationSet(ballScene->transformAnimations());
	if (hgr::UserPropertySet* props = ballScene->userProperties()) {
		int eyeTriggers = 0;
		for (const char* name : { "T_bounce_lookat_eyes_anim01", "T_bounce_lookat_eyes_anim02" }) {
			if (!props->containsKey(name) || m_world->triggerState(name) >= 0) continue;
			m_world->addTrigger(name, props->get(name).c_str());
			m_world->setTriggerEnabled(name, true);
			m_world->activateTrigger(name);
			++eyeTriggers;
		}
		if (eyeTriggers) printf("ball: %d eye animation triggers (blinking, eyebrows)\n", eyeTriggers);
	}
	applyModelPose(ballScene);
	m_player->setBallModel(m_ballRoot);
	m_player->createShadow(m_ctx);
	m_player->playSound = [this](const std::string& file, float volume) { if (playPlayerSound) playPlayerSound(file, volume); };
	if (m_world->triggerState("T_butterflyanim") >= 0) m_world->activateTrigger("T_butterflyanim");
	m_player->loadRails();
	if (m_player->hasRails()) {
		if (hgr::UserPropertySet* props = m_scene->userProperties())
			for (lang::HashtableIterator<lang::String, lang::String> it = props->begin(); it != props->end(); ++it)
				if (std::string(it.value().c_str()).find("skybox=1") != std::string::npos)
					if (hgr::Node* sky = m_scene->findNodeByName(it.key())) m_skyNode = sky;
		if (DIR* dir = opendir(m_options.particleDir().c_str())) {
			while (struct dirent* entry = readdir(dir)) {
				const std::string name = entry->d_name, lower = toLower(name);
				if (lower.find("boost") != std::string::npos && endsWith(lower, ".prs")) m_boostParticles = name;
			}
			closedir(dir);
		}
	}
	m_world->registerNode("G_ballforms", m_ballRoot.ptr());
	if (m_world->triggerState("T_restartLevel") >= 0) {
		m_world->setTriggerEnabled("T_restartLevel", true);
		m_world->activateTrigger("T_restartLevel");
	}
	m_player->spawn();
	printf("%s\n", m_player->spawnInfo.c_str());
}

void LevelCore::setupCamera() {
	m_camera = new hgr::Camera;
	m_camera->setName("view");
	m_camera->setHorizontalFov(1.3f);
	m_camera->setFront(1.f);
	m_camera->setBack(100000.f);
	m_camera->linkTo(m_scene);
	for (hgr::Camera* c : m_cameras) {
		const std::string name = c->name().c_str();
		if ((!m_options.cameraName.empty() && name == m_options.cameraName) || (m_options.cameraName.empty() && name.find("startcamera") != std::string::npos)) {
			const math::float3x4 w = c->worldTransform();
			m_position = w.translation();
			const math::float3 forward = w.getColumn(2);
			m_yaw = atan2f(forward.x, forward.z);
			m_pitch = asinf(std::max(-1.f, std::min(1.f, forward.y)));
		}
	}
}

void LevelCore::cycleDevCamera() {
	if (m_cameras.empty()) return;
	m_devCamera = m_devCamera + 1 >= (int)m_cameras.size() ? -1 : m_devCamera + 1;
	printf("camera: %s\n", m_devCamera < 0 ? "free" : m_cameras[m_devCamera]->name().c_str());
}

void LevelCore::toggleHelpers() {
	m_helpersVisible = !m_helpersVisible;
	for (hgr::Node* n : m_helpers) n->setEnabled(m_helpersVisible);
}

void LevelCore::toggleFreeFly() {
	if (m_player) m_freeFly = !m_freeFly;
	m_followGameCamera = !m_followGameCamera;
}

void LevelCore::respawn() {
	if (m_player) m_player->spawn();
}

math::float3 LevelCore::listenerPosition() const {
	if (m_player) return math::float3(m_player->ball().position.x, m_player->ball().position.y, m_player->ball().position.z);
	return m_camera->worldTransform().translation();
}

void LevelCore::updateWorld(float dt, bool tap) {
	m_world->setKeyDown("lmouse", tap);
	const bool arcadeOver = m_player && m_player->onRail() && m_world->levelCompleted();
	if (!arcadeOver) m_world->update(dt);
	if (!traceEnabled()) return;
	std::string running;
	const std::vector<std::string> triggers = m_world->runningTriggers();
	for (size_t i = 0; i < triggers.size(); ++i) running += (i ? ", " : "") + triggers[i];
	if (running != m_lastRunning) {
		printf("[trace] running triggers: %s\n", running.empty() ? "(none)" : running.c_str());
		m_lastRunning = running;
	}
}

void LevelCore::updatePlayer(float dt, const LevelInput& input) {
	const PlayerCommand& cmd = input.command;
	m_player->setGuidedCamera(input.guidedCamera);
	m_player->setModernInput(cmd.modern, cmd.moveX, cmd.moveY, cmd.cameraYaw, cmd.cameraPitch, cmd.followPath, cmd.autoFollow);
	m_player->update(dt, cmd.forward, cmd.turn, cmd.jump);
	m_player->updateChaseCamera(m_camera, dt);
	m_position = m_camera->worldTransform().translation();
	const math::float3 dir = m_camera->worldTransform().getColumn(2);
	m_yaw = atan2f(dir.x, dir.z);
	m_pitch = asinf(std::max(-1.f, std::min(1.f, dir.y)));
}

void LevelCore::updateFreeCamera(float dt, const FreeFlyInput& fly) {
	m_yaw += fly.yaw;
	m_pitch += fly.pitch;
	m_pitch = std::max(-1.5f, std::min(1.5f, m_pitch));
	const math::float3 forward(sinf(m_yaw) * cosf(m_pitch), sinf(m_pitch), cosf(m_yaw) * cosf(m_pitch));
	const math::float3 right(cosf(m_yaw), 0, -sinf(m_yaw));
	const float speed = (fly.fast ? 2000.f : 400.f) * dt;
	m_position += forward * (fly.forward * speed);
	m_position += right * (fly.right * speed);
	m_position.y += fly.up * speed;
	m_camera->setPosition(m_position);
	m_camera->lookAt(m_position + forward);
}

void LevelCore::spawnBoostParticles() {
	if (!m_player || m_boostParticles.empty()) return;
	const bool boost = m_world->getVariable("boost") != 0.f;
	if (boost && !m_boostWas) {
		try {
			hgr::ParticleSystem* source = m_resources->getParticleSystem((m_options.particleDir() + m_boostParticles).c_str(),
			                                                             m_options.textureDir().c_str(), m_options.shaderDir().c_str());
			if (source) {
				P(hgr::ParticleSystem) ps = new hgr::ParticleSystem(*source);
				math::float3x4 tm(1.f);
				tm.setTranslation(m_player->modelCenter());
				ps->setTransform(tm);
				ps->linkTo(m_ballRoot);
				m_particles.push_back(std::make_pair(ps, 3.f));
			}
		} catch (lang::Throwable& e) {
			fprintf(stderr, "particles '%s': %s\n", m_boostParticles.c_str(), e.getMessage().format().c_str());
		}
	}
	m_boostWas = boost;
}

hgr::Camera* LevelCore::activeCamera() {
	hgr::Camera* cam = m_devCamera < 0 ? (hgr::Camera*)m_camera : m_cameras[m_devCamera];
	if (m_followGameCamera && !m_world->activeCamera().empty())
		for (hgr::Camera* c : m_cameras)
			if (m_world->activeCamera() == c->name().c_str()) cam = c;
	if (m_player && !m_freeFly && (m_world->activeCamera().empty() || m_world->activeCamera() == "camera")) cam = m_camera;
	if (m_player && cam == m_camera && m_world->hasVariable("fov")) {
		if (m_baseFov < 0.f) m_baseFov = m_camera->horizontalFov();
		const float want = m_baseFov + m_world->getVariable("fov") * 3.14159265f / 180.f;
		if (std::fabs(m_camera->horizontalFov() - want) > 1e-3f) m_camera->setHorizontalFov(std::min(want, 2.6f));
	}
	return cam;
}

bool LevelCore::update(float dt, float realDt, const LevelInput& input, bool resultsAllowed) {
	for (hgr::Node* n = m_scene->firstChild(); n; n = n->next(m_scene))
		if (n->classId() == hgr::Node::NODE_PARTICLESYSTEM) static_cast<hgr::ParticleSystem*>(n)->update(dt);
	for (size_t i = 0; i < m_particles.size();) {
		m_particles[i].second -= dt;
		if (m_particles[i].second <= 0.f) {
			m_particles[i].first->unlink();
			m_particles.erase(m_particles.begin() + i);
		} else ++i;
	}
	bool showResults = false;
	if (m_world->levelCompleted() && !m_resultsShown && resultsAllowed) {
		m_completeTimer += realDt;
		const float wait = (m_player && m_player->onRail()) ? 0.f : 1.5f;
		if (m_completeTimer >= wait) {
			m_resultsShown = true;
			showResults = true;
		}
	}
	updateWorld(dt, input.tap);
	if (m_player && !m_freeFly) updatePlayer(dt, input);
	else updateFreeCamera(dt, input.fly);
	spawnBoostParticles();
	return showResults;
}

}
