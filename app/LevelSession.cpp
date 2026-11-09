#include "LevelSession.h"
#include "AndroidBridge.h"
#include "Icons.h"
#include "I18n.h"
#include <cstdarg>
#include "AppContext.h"
#include "Audio.h"
#include "BallController.h"
#include "Config.h"
#include "Devices.h"
#include "FileUtils.h"
#include "GLES2.h"
#include "Overlay.h"
#include "SceneWorld.h"
#include "Ui.h"
#include "WaterReflection.h"
#include <hgr/Camera.h>
#include <hgr/DefaultResourceManager.h>
#include <hgr/Dummy.h>
#include <hgr/Mesh.h>
#include <hgr/ParticleSystem.h>
#include <hgr/Scene.h>
#include <hgr/SceneInputStream.h>
#include <hgr/TransformAnimation.h>
#include <hgr/TransformAnimationSet.h>
#include <hgr/UserPropertySet.h>
#include <io/FileInputStream.h>
#include <lang/Format.h>
#include <lang/Hashtable.h>
#include <lang/Throwable.h>
#include <math/float3x3.h>
#include <math/quaternion.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

namespace redbound {

static void swapWindow(void* window) { SDL_GL_SwapWindow((SDL_Window*)window); }

static bool traceEnabled() { return getenv("REDBOUND_TRACE") != 0; }

LevelSession::LevelSession(AppContext& app, const LaunchOptions& options) : m_app(app), m_options(options), m_position(0, 200, -600) {}

LevelSession::~LevelSession() {}

void LevelSession::requestLevel(const std::vector<std::string>& args) {
	m_result.next = args;
	m_running = false;
}

void LevelSession::showLoadingScreen() {
	m_ui.reset(new Ui);
	const std::string& data = m_options.dataDir;
	if (!data.empty()) {
		m_hasUi = m_ui->loadSheet(data + "/resources/images/MENU.dat");
		m_ui->loadSheet(data + "/resources/images/INGAME.dat");
		m_ui->loadSheet(data + "/resources/images/LOADING.dat");
		m_ui->loadFont(data + "/resources/localization/FONT.dat");
		m_ui->loadFont(data + "/resources/localization/FONT_YELLOW.dat");
	}
	if (!m_hasUi) return;
	glViewport(0, 0, m_app.width, m_app.height);
	glClearColor(1.f, 1.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	m_ui->begin(m_app.width, m_app.height);
	if (!m_options.fromMenu) m_ui->drawSprite("LOADING", "LOADING_ROVIO", m_ui->width() * 0.5f, 225.f);
	m_ui->drawSprite("LOADING", "LOADING_TEXT", m_ui->width() - 135.f, 451.f);
	m_ui->end();
	SDL_GL_SwapWindow(m_app.window);
}

void LevelSession::loadScene() {
	m_ctx = new gr::GLES2_Context(m_app.width, m_app.height, swapWindow, m_app.window);
	{
		io::FileInputStream in(m_options.levelFile.c_str());
		hgr::SceneInputStream header(&in);
		m_ctx->setPlatformId((int)header.platform());
	}
	m_ctx->setShaderDir(m_options.shaderDir());
	m_ctx->setTextureDir(m_options.textureDir());
	m_resources = new hgr::DefaultResourceManager(m_ctx);
	showLoadingScreen();
	const Uint32 start = SDL_GetTicks();
	m_scene = new hgr::Scene(m_ctx, m_options.levelFile.c_str(), m_resources, m_options.textureDir().c_str(), m_options.shaderDir().c_str(),
	                         m_options.particleDir().c_str());
	printf("level loaded in %u ms | textures: %d ok, %d failed | effects: %d ok, %d fallback\n", SDL_GetTicks() - start,
	       m_ctx->texturesLoaded, m_ctx->texturesFailed, m_ctx->effectsLoaded, m_ctx->effectsFallback);
	loadLibraryObjects(m_scene, m_ctx, m_resources, m_options, m_libraryObjects);
	applyFog(m_scene, m_ctx);
	m_helpers = hideHelperNodes(m_scene);
	m_cameras = collectCameras(m_scene);
	m_clearColor = backgroundColor(m_scene);
	m_audio.reset(new Audio);
	Config& cfg = Config::get();
	if (!getenv("REDBOUND_NO_AUDIO") && !m_options.dataDir.empty()) m_audio->init(m_options.dataDir);
	applyAudioSettings(*m_audio);
	m_app.overlay->setAudio(m_audio.get());
	m_ambient.load(m_scene, *m_audio);
	if (m_options.menu || (m_options.fromMenu && m_options.play && m_options.levelFile.find("level01") != std::string::npos)) m_hud.startLogo();
}

void LevelSession::setupWorld() {
	m_world.reset(new SceneWorld(m_scene));
	if (m_options.menu) m_world->setVariable("attraction", 1.f);
	for (const auto& v : m_options.variables) m_world->setVariable(v.first, v.second);
	int collectables = 0;
	for (LibraryObject& object : m_libraryObjects) {
		if (object.properties.find("a01") == std::string::npos) continue;
		m_world->registerNode(object.marker, object.visual);
		std::string props = object.properties;
		for (size_t at = props.find(",this"); at != std::string::npos; at = props.find(",this", at + 1)) {
			const char next = at + 5 < props.size() ? props[at + 5] : '\n';
			if (next == '\n' || next == '\r' || next == ',') props.replace(at + 1, 4, object.marker);
		}
		m_world->addTrigger(object.trigger, props);
		if (props.find("item=collectable") != std::string::npos) ++collectables;
	}
	m_hud.setCollectableTotal(collectables);
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
	m_world->playSound = [this](const std::string& file, bool loop) { m_audio->play(file, loop); };
	m_world->stopSound = [this](const std::string& file) { m_audio->stop(file); };
	m_world->onPressure = [](float strength) { Devices::instance().rumble(strength); };
	printf("logic: %d triggers, %d warnings\n", (int)m_world->triggerCount(), (int)m_world->warnings().size());
	for (size_t i = 0; i < m_world->warnings().size() && i < 10; ++i) printf("  warning: %s\n", m_world->warnings()[i].c_str());
}

void LevelSession::applyModelPose(hgr::Scene* ballScene) {
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

void LevelSession::setupPlayer() {
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
	m_player->playSound = [this](const std::string& file, float volume) { m_audio->play(file, false, volume); };
	if (m_world->triggerState("T_butterflyanim") >= 0) m_world->activateTrigger("T_butterflyanim");
	m_player->loadRails();
	if (m_player->hasRails()) {
		if (hgr::UserPropertySet* props = m_scene->userProperties())
			for (lang::HashtableIterator<lang::String, lang::String> it = props->begin(); it != props->end(); ++it)
				if (std::string(it.value().c_str()).find("skybox=1") != std::string::npos)
					if (hgr::Node* sky = m_scene->findNodeByName(it.key()))
						if (m_sky.init(sky))
							for (hgr::Node* c = sky; c; c = c->next(sky)) c->setEnabled(false);
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

void LevelSession::setupCamera() {
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

static std::string formatText(const char* fmt, const char* value) {
	char buffer[256];
	snprintf(buffer, sizeof(buffer), fmt, value);
	return buffer;
}

void LevelSession::pauseForBackground() {
	if (!m_audio->paused()) printf("app: in the background, game paused and audio muted\n");
	m_audio->setPaused(true);
	if (m_hasUi && (m_options.menu || m_options.fromMenu) && m_player && !m_results && !m_menu.isOpen()) {
		m_menu.open();
		if (!m_ui->hasCapture()) m_needCapture = true;
	}
}

bool LevelSession::handlePauseEvent(const SDL_Event& e) {
	if (!m_hasUi || !(m_options.menu || m_options.fromMenu) || m_menu.isOpen()) return false;
	const bool clickPause = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
	                        (!m_player || m_results || (e.button.x < m_app.width * 0.12f && e.button.y < m_app.height * 0.15f));
	const bool keyPause = (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_p || (m_results && e.key.keysym.sym == SDLK_RETURN))) ||
	                      (e.type == SDL_CONTROLLERBUTTONDOWN &&
	                       (e.cbutton.button == SDL_CONTROLLER_BUTTON_START || (m_results && e.cbutton.button == SDL_CONTROLLER_BUTTON_A)));
	if (!clickPause && !keyPause) return false;
	m_menu.open();
	if (!m_ui->hasCapture()) m_needCapture = true;
	return true;
}

void LevelSession::handleMenuAction(MenuAction action) {
	const std::string dir = m_options.levelDir();
	switch (action) {
	case MenuAction::Attraction: requestLevel({ "--menu", "--from-menu", dir + "level01.hgr" }); break;
	case MenuAction::Level1: requestLevel({ "--play", "--from-menu", dir + "level01.hgr" }); break;
	case MenuAction::Level2: requestLevel({ "--play", "--from-menu", dir + "level02.hgr" }); break;
	case MenuAction::ToggleVolume: m_audio->setMuted(!m_menu.volumeOn()); break;
	case MenuAction::Minimize: SDL_MinimizeWindow(m_app.window); break;
	case MenuAction::Quit: m_result.quit = true; m_running = false; break;
	default: break;
	}
}

void LevelSession::handleDevKey(SDL_Keycode key) {
	if (key == SDLK_TAB && !m_cameras.empty()) {
		m_devCamera = m_devCamera + 1 >= (int)m_cameras.size() ? -1 : m_devCamera + 1;
		printf("camera: %s\n", m_devCamera < 0 ? "free" : m_cameras[m_devCamera]->name().c_str());
	} else if (key == SDLK_F3) {
		m_helpersVisible = !m_helpersVisible;
		for (hgr::Node* n : m_helpers) n->setEnabled(m_helpersVisible);
	} else if (key == SDLK_F4) {
		if (m_player) m_freeFly = !m_freeFly;
		m_followGameCamera = !m_followGameCamera;
	} else if (key == SDLK_F5) {
		m_water->enabled = !m_water->enabled;
	} else if (key == SDLK_r && m_player) {
		m_player->spawn();
	} else if (key == SDLK_F12) {
		char name[64];
		snprintf(name, sizeof(name), "screenshot-%u.png", SDL_GetTicks());
		if (m_ctx->saveScreenshot(name)) printf("saved %s\n", name);
	}
}

void LevelSession::handleEvents() {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (Devices::instance().handleEvent(e)) continue;
		if (e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_DIDENTERBACKGROUND ||
		    (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_MINIMIZED))) {
			setMouseCapture(false);
			pauseForBackground();
		} else if (e.type == SDL_APP_DIDENTERFOREGROUND ||
		           (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_RESTORED))) {
			if (m_audio->paused()) {
				m_audio->setPaused(false);
				printf("app: back in the foreground, audio resumed\n");
			}
		}
		if (e.type == SDL_CONTROLLERDEVICEADDED) {
			const char* name = SDL_GameControllerNameForIndex(e.cdevice.which);
			m_app.overlay->notify(formatText(tr("notice.controller_connected"), name ? name : "?"), icon::gamepad);
		} else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
			m_app.overlay->notify(tr("notice.controller_disconnected"), icon::gamepad);
		}
		if (m_app.overlay->processEvent(e)) continue;
		if (m_player && !m_menu.isOpen() && !m_results) m_input.handleTouch(e, m_app.width, m_app.height);
		if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && m_menu.isOpen()) {
			const float v = e.caxis.value / 32767.f;
			SDL_Keycode key = 0;
			if (m_stickNav == 0 && v > 0.6f) { key = SDLK_RIGHT; m_stickNav = 1; }
			else if (m_stickNav == 0 && v < -0.6f) { key = SDLK_LEFT; m_stickNav = -1; }
			else if (std::fabs(v) < 0.3f) m_stickNav = 0;
			if (key) {
				SDL_Event k;
				memset(&k, 0, sizeof(k));
				k.type = SDL_KEYDOWN;
				k.key.keysym.sym = key;
				SDL_PushEvent(&k);
			}
			continue;
		}
		if (e.type == SDL_CONTROLLERBUTTONDOWN && m_menu.isOpen()) {
			SDL_Keycode key = 0;
			switch (e.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key = SDLK_LEFT; break;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = SDLK_RIGHT; break;
			case SDL_CONTROLLER_BUTTON_A: key = SDLK_RETURN; break;
			case SDL_CONTROLLER_BUTTON_B: key = SDLK_p; break;
			}
			if (key) {
				SDL_Event k;
				memset(&k, 0, sizeof(k));
				k.type = SDL_KEYDOWN;
				k.key.keysym.sym = key;
				SDL_PushEvent(&k);
				continue;
			}
		}
		if (handlePauseEvent(e)) continue;
		if (m_menu.isOpen() && m_hasUi) {
			const bool pointer = e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN;
			const bool key = e.type == SDL_KEYDOWN;
			if (pointer || key) {
				bool resumed = false;
				const MenuAction action = m_menu.handleEvent(e, *m_ui, m_app.height, resumed);
				if (resumed) {
					Devices::instance().requestRecalibration();
					if (!m_results) m_ui->clearCapture();
				}
				handleMenuAction(action);
				if (pointer || (e.key.keysym.sym != SDLK_F12 && e.key.keysym.sym != SDLK_m)) continue;
			}
		}
		if (e.type == SDL_QUIT) {
			m_result.quit = true;
			m_running = false;
		} else if (e.type == SDL_KEYDOWN) {
			if (e.key.keysym.sym == SDLK_m) m_audio->setMuted(!m_audio->muted());
			else if (m_options.dev) handleDevKey(e.key.keysym.sym);
		} else if (e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID) {
			if (m_mouseLook) {
				if (m_player && !m_freeFly) m_input.addMouseOrbit((float)e.motion.xrel, (float)e.motion.yrel);
				else {
					m_yaw += e.motion.xrel * 0.004f;
					m_pitch -= e.motion.yrel * 0.004f;
				}
			} else if (mouseCaptureAllowed()) {
				m_mouseTravel += std::abs(e.motion.xrel) + std::abs(e.motion.yrel);
				if (m_mouseTravel > 24) setMouseCapture(true);
			}
		}
	}
}

bool LevelSession::mouseCaptureAllowed() const {
	const bool focused = (SDL_GetWindowFlags(m_app.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
	return focused && (m_freeFly || (m_player && !m_results && !m_menu.isOpen() && !m_app.overlay->open()));
}

void LevelSession::setMouseCapture(bool capture) {
	if (capture == m_mouseLook) return;
	m_mouseLook = capture;
	m_mouseTravel = 0;
	SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
	m_input.setMouseCaptured(capture);
}

void LevelSession::updateWorld(float dt) {
	const Uint8* k = SDL_GetKeyboardState(0);
	const bool tap = (SDL_GetMouseState(0, 0) & SDL_BUTTON(SDL_BUTTON_LEFT)) || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_5] ||
	                 k[SDL_SCANCODE_KP_0] || m_input.anyJumpButton();
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

void LevelSession::updatePlayer(float dt) {
	const PlayerCommand cmd = m_input.update(dt, m_app.width, m_app.height, m_player->controllable());
	const bool tilt = PlayerInput::touchScheme() == "accelerometer";
	if (tilt != m_orientationLocked) {
		m_orientationLocked = tilt;
		androidSetOrientationLocked(tilt);
		if (tilt) Devices::instance().requestRecalibration();
	}
	m_player->setGuidedCamera(tilt);
	m_player->setModernInput(cmd.modern, cmd.moveX, cmd.moveY, cmd.cameraYaw, cmd.cameraPitch, cmd.followPath, cmd.autoFollow);
	m_player->update(dt, cmd.forward, cmd.turn, cmd.jump);
	m_player->updateChaseCamera(m_camera, dt);
	m_position = m_camera->worldTransform().translation();
	const math::float3 dir = m_camera->worldTransform().getColumn(2);
	m_yaw = atan2f(dir.x, dir.z);
	m_pitch = asinf(std::max(-1.f, std::min(1.f, dir.y)));
}

void LevelSession::updateFreeCamera(float dt) {
	const Uint8* k = SDL_GetKeyboardState(0);
	if (k[SDL_SCANCODE_LEFT]) m_yaw -= 1.5f * dt;
	if (k[SDL_SCANCODE_RIGHT]) m_yaw += 1.5f * dt;
	if (k[SDL_SCANCODE_UP]) m_pitch += 1.2f * dt;
	if (k[SDL_SCANCODE_DOWN]) m_pitch -= 1.2f * dt;
	m_pitch = std::max(-1.5f, std::min(1.5f, m_pitch));
	const math::float3 forward(sinf(m_yaw) * cosf(m_pitch), sinf(m_pitch), cosf(m_yaw) * cosf(m_pitch));
	const math::float3 right(cosf(m_yaw), 0, -sinf(m_yaw));
	const float speed = (k[SDL_SCANCODE_LSHIFT] ? 2000.f : 400.f) * dt;
	if (k[SDL_SCANCODE_W]) m_position += forward * speed;
	if (k[SDL_SCANCODE_S]) m_position -= forward * speed;
	if (k[SDL_SCANCODE_D]) m_position += right * speed;
	if (k[SDL_SCANCODE_A]) m_position -= right * speed;
	if (k[SDL_SCANCODE_E]) m_position.y += speed;
	if (k[SDL_SCANCODE_Q]) m_position.y -= speed;
	m_camera->setPosition(m_position);
	m_camera->lookAt(m_position + forward);
}

void LevelSession::spawnBoostParticles() {
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

hgr::Camera* LevelSession::activeCamera() {
	hgr::Camera* cam = m_devCamera < 0 ? (hgr::Camera*)m_camera : m_cameras[m_devCamera];
	if (m_followGameCamera && !m_world->activeCamera().empty())
		for (hgr::Camera* c : m_cameras)
			if (m_world->activeCamera() == c->name().c_str()) cam = c;
	if (m_player && !m_freeFly && (m_world->activeCamera().empty() || m_world->activeCamera() == "camera")) cam = m_camera;
	if (m_player && cam == m_camera && m_world->hasVariable("fov")) {
		static float baseFov = -1.f;
		if (baseFov < 0.f) baseFov = m_camera->horizontalFov();
		const float want = baseFov + m_world->getVariable("fov") * 3.14159265f / 180.f;
		if (std::fabs(m_camera->horizontalFov() - want) > 1e-3f) m_camera->setHorizontalFov(std::min(want, 2.6f));
	}
	return cam;
}

void LevelSession::update(float dt, float realDt) {
	for (hgr::Node* n = m_scene->firstChild(); n; n = n->next(m_scene))
		if (n->classId() == hgr::Node::NODE_PARTICLESYSTEM) static_cast<hgr::ParticleSystem*>(n)->update(dt);
	for (size_t i = 0; i < m_particles.size();) {
		m_particles[i].second -= dt;
		if (m_particles[i].second <= 0.f) {
			m_particles[i].first->unlink();
			m_particles.erase(m_particles.begin() + i);
		} else ++i;
	}
	math::float3 listener = m_camera->worldTransform().translation();
	if (m_player) listener = math::float3(m_player->ball().position.x, m_player->ball().position.y, m_player->ball().position.z);
	m_ambient.update(m_scene, *m_audio, listener);
	if (m_world->levelCompleted() && !m_results && m_hasUi) {
		m_completeTimer += realDt;
		const float wait = (m_player && m_player->onRail()) ? 0.f : 1.5f;
		if (m_completeTimer >= wait) {
			m_results = true;
			m_hud.startResults();
			m_ui->clearCapture();
			m_needCapture = true;
		}
	}
	updateWorld(dt);
	if (m_player && !m_freeFly) updatePlayer(dt);
	else updateFreeCamera(dt);
	spawnBoostParticles();
}

void LevelSession::render(float realDt, bool paused, bool liveBlur) {
	hgr::Camera* cam = activeCamera();
	const int w = m_app.width, h = m_app.height;
	const float scale = std::max(0.5f, std::min(1.f, Config::get().num("graphics.render_scale", 1.f)));
	const int sw = std::max(1, (int)(w * scale)), sh = std::max(1, (int)(h * scale));
	const bool scaled = scale < 0.99f && m_sceneTarget.begin(sw, sh);
	const int rw = scaled ? sw : w, rh = scaled ? sh : h;
	if (scaled) m_ctx->setDefaultFramebuffer(m_sceneTarget.framebuffer(), sw, sh);
	m_ctx->beginScene();
	m_water->update(cam, rw, rh, m_ctx->time, 0xFF6688AA);
	m_ctx->setViewport(gr::Rect(0, 0, rw, rh));
	m_ctx->clear(m_clearColor);
	m_sky.draw(cam, rw, rh);
	cam->render(m_ctx);
	if (scaled) {
		m_ctx->setDefaultFramebuffer(0);
		m_sceneTarget.end(w, h);
		m_ctx->setViewport(gr::Rect(0, 0, w, h));
	}
	if (m_followGameCamera) m_fade.draw(m_world->fadeAmount(), m_world->fadeColor() == 0);
	if (m_hasUi) {
		Ui& ui = *m_ui;
		if (m_needCapture) {
			ui.begin(w, h);
			ui.captureScreen();
			ui.end();
			m_needCapture = false;
		}
		if ((paused || liveBlur) && ui.hasCapture()) {
			ui.begin(w, h);
			ui.drawCaptured(4.f, 1.f, 0.6f);
			ui.end();
		}
		ui.begin(w, h);
		m_hud.draw(ui, *m_world, m_player.get(), paused, m_results, m_menu.isOpen(), realDt);
		m_hud.drawLogo(ui, paused, realDt);
		if (Config::get().flag("graphics.show_fps", false)) {
			char fps[32];
			snprintf(fps, sizeof(fps), "%d", (int)(m_fps + 0.5f));
			const float labelPx = 17.f * ui.scale();
			const float labelWidth = m_app.overlay->labelWidth("FPS", labelPx) / ui.scale();
			const float right = ui.width() - 18.f, centerY = ui.height() - 22.f;
			ui.drawString("FONT", fps, right - labelWidth - 6.f, centerY, 2, 1);
			m_app.overlay->addLabel("FPS", right * ui.scale(), (centerY + 2.f) * ui.scale(), labelPx);
		}
		m_menu.draw(ui);
		ui.end();
	}
	m_ctx->endScene();
}

SessionResult LevelSession::run() {
	loadScene();
	setupWorld();
	if (m_options.play && !m_options.dataDir.empty()) {
		try {
			setupPlayer();
		} catch (lang::Throwable& e) {
			fprintf(stderr, "could not load the ball: %s\n", e.getMessage().format().c_str());
			m_player.reset();
		}
	}
	m_water.reset(new WaterReflection(m_ctx, m_scene));
	if (m_player) {
		WaterReflection* water = m_water.get();
		m_player->setWaterQuery([water](float x, float z, float& height) { return water->surfaceAt(x, z, height); });
	}
	setupCamera();
	Devices::instance().openAll();
	m_app.overlay->onQuit = [this]() {
		m_result.quit = true;
		m_running = false;
	};
	Uint32 last = SDL_GetTicks();
	while (m_running) {
		handleEvents();
		if (m_mouseLook && !mouseCaptureAllowed()) setMouseCapture(false);
		switch (m_input.takeDeviceChange()) {
		case PlayerInput::Device::Keyboard: m_app.overlay->notify(tr("notice.input_keyboard"), icon::keyboard); break;
		case PlayerInput::Device::Mouse: m_app.overlay->notify(tr("notice.input_mouse"), icon::mouse); break;
		case PlayerInput::Device::Controller: m_app.overlay->notify(tr("notice.input_controller"), icon::gamepad); break;
		case PlayerInput::Device::Touch: m_app.overlay->notify(tr("notice.input_touch"), icon::hand); break;
		case PlayerInput::Device::Tilt: m_app.overlay->notify(tr("notice.input_tilt"), icon::phone); break;
		default: break;
		}
		if (!m_running) break;
		int dw, dh;
		SDL_GL_GetDrawableSize(m_app.window, &dw, &dh);
		if (dw != m_app.width || dh != m_app.height) {
			m_app.width = dw;
			m_app.height = dh;
			m_ctx->resize(dw, dh);
		}
		const Uint32 now = SDL_GetTicks();
		const float realDt = std::min(0.1f, (now - last) / 1000.f);
		m_fpsFrames++;
		m_fpsTime += (now - last) / 1000.f;
		if (m_fpsTime >= 0.5f) {
			m_fps = m_fpsFrames / m_fpsTime;
			m_fpsFrames = 0;
			m_fpsTime = 0.f;
		}
		last = now;
		const bool paused = (m_menu.isOpen() && m_player && !m_results) || m_app.overlay->open();
		const bool liveBlur = (m_menu.isOpen() || m_results) && !paused;
		if (liveBlur && m_hasUi) m_needCapture = true;
		const float dt = paused ? 0.f : realDt;
		m_ctx->time += dt;
		update(dt, realDt);
		render(realDt, paused, liveBlur);
		const int fpsLimit = (int)Config::get().num("graphics.fps_limit", 0.f);
		if (fpsLimit > 0) {
			const Uint32 frameMs = 1000 / (Uint32)fpsLimit, spent = SDL_GetTicks() - now;
			if (spent < frameMs) SDL_Delay(frameMs - spent);
		}
		++m_frame;
		if (!m_options.screenshot.empty() && m_frame >= (m_options.frames > 0 ? m_options.frames : 3)) {
			printf(m_ctx->saveScreenshot(m_options.screenshot) ? "screenshot: %s\n" : "could not save %s\n", m_options.screenshot.c_str());
			m_result.quit = true;
			m_running = false;
		} else if (m_options.frames > 0 && m_frame >= m_options.frames) {
			m_result.quit = true;
			m_running = false;
		}
		m_app.overlay->draw();
		m_ctx->present();
	}
	setMouseCapture(false);
	if (m_orientationLocked) androidSetOrientationLocked(false);
	m_app.overlay->setAudio(0);
	return m_result;
}

}
