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

LevelSession::LevelSession(AppContext& app, const LaunchOptions& options) : m_app(app), m_options(options) {}

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
	m_core.reset(new LevelCore(m_ctx, m_resources, m_options));
	m_core->loadScene();
	printf("level loaded in %u ms | textures: %d ok, %d failed | effects: %d ok, %d fallback\n", SDL_GetTicks() - start,
	       m_ctx->texturesLoaded, m_ctx->texturesFailed, m_ctx->effectsLoaded, m_ctx->effectsFallback);
	const LevelFog& fog = m_core->fog();
	if (fog.enabled) {
		m_ctx->fogRange = math::float4(fog.start, fog.end, 1.f / (fog.end - fog.start), 0.f);
		m_ctx->fogColor = math::float4(fog.color[0], fog.color[1], fog.color[2], 1.f);
	} else {
		m_ctx->fogRange = math::float4(0, 0, 0, 0);
	}
	m_audio.reset(new Audio);
	Config& cfg = Config::get();
	if (!getenv("REDBOUND_NO_AUDIO") && !m_options.dataDir.empty()) m_audio->init(m_options.dataDir);
	applyAudioSettings(*m_audio);
	m_app.overlay->setAudio(m_audio.get());
	m_ambient.load(m_core->scene(), *m_audio);
	if (m_options.menu || (m_options.fromMenu && m_options.play && m_options.levelFile.find("level01") != std::string::npos)) m_hud.startLogo();
}

void LevelSession::setupWorld() {
	m_core->playSound = [this](const std::string& file, bool loop) { m_audio->play(file, loop); };
	m_core->stopSound = [this](const std::string& file) { m_audio->stop(file); };
	m_core->playPlayerSound = [this](const std::string& file, float volume) { m_audio->play(file, false, volume); };
	m_core->onPressure = [](float strength) { Devices::instance().rumble(strength); };
	m_core->setupWorld();
	m_hud.setCollectableTotal(m_core->collectableTotal());
}

void LevelSession::setupPlayer() {
	m_core->setupPlayer();
	if (hgr::Node* sky = m_core->skyNode())
		if (m_sky.init(sky))
			for (hgr::Node* c = sky; c; c = c->next(sky)) c->setEnabled(false);
}

static std::string formatText(const char* fmt, const char* value) {
	char buffer[256];
	snprintf(buffer, sizeof(buffer), fmt, value);
	return buffer;
}

void LevelSession::pauseForBackground() {
	if (!m_audio->paused()) printf("app: in the background, game paused and audio muted\n");
	m_audio->setPaused(true);
	if (m_hasUi && (m_options.menu || m_options.fromMenu) && m_core->player() && !m_results && !m_menu.isOpen()) {
		m_menu.open();
		if (!m_ui->hasCapture()) m_needCapture = true;
	}
}

bool LevelSession::handlePauseEvent(const SDL_Event& e) {
	if (!m_hasUi || !(m_options.menu || m_options.fromMenu) || m_menu.isOpen()) return false;
	const bool clickPause = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
	                        (!m_core->player() || m_results || (e.button.x < m_app.width * 0.12f && e.button.y < m_app.height * 0.15f));
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
	if (key == SDLK_TAB) {
		m_core->cycleDevCamera();
	} else if (key == SDLK_F3) {
		m_core->toggleHelpers();
	} else if (key == SDLK_F4) {
		m_core->toggleFreeFly();
	} else if (key == SDLK_F5) {
		m_water->enabled = !m_water->enabled;
	} else if (key == SDLK_r) {
		m_core->respawn();
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
		if (m_core->player() && !m_menu.isOpen() && !m_results) m_input.handleTouch(e, m_app.width, m_app.height);
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
				if (m_core->player() && !m_core->freeFly()) m_input.addMouseOrbit((float)e.motion.xrel, (float)e.motion.yrel);
				else {
					m_mouseYaw += e.motion.xrel * 0.004f;
					m_mousePitch -= e.motion.yrel * 0.004f;
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
	return focused && (m_core->freeFly() || (m_core->player() && !m_results && !m_menu.isOpen() && !m_app.overlay->open()));
}

void LevelSession::setMouseCapture(bool capture) {
	if (capture == m_mouseLook) return;
	m_mouseLook = capture;
	m_mouseTravel = 0;
	SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
	m_input.setMouseCaptured(capture);
}

LevelInput LevelSession::readInput(float dt) {
	LevelInput input;
	const Uint8* k = SDL_GetKeyboardState(0);
	input.tap = (SDL_GetMouseState(0, 0) & SDL_BUTTON(SDL_BUTTON_LEFT)) || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_5] ||
	            k[SDL_SCANCODE_KP_0] || m_input.anyJumpButton();
	BallController* player = m_core->player();
	if (player && !m_core->freeFly()) {
		input.command = m_input.update(dt, m_app.width, m_app.height, player->controllable());
		const bool tilt = PlayerInput::touchScheme() == "accelerometer";
		if (tilt != m_orientationLocked) {
			m_orientationLocked = tilt;
			androidSetOrientationLocked(tilt);
			if (tilt) Devices::instance().requestRecalibration();
		}
		input.guidedCamera = tilt;
	} else {
		FreeFlyInput& fly = input.fly;
		fly.yaw = m_mouseYaw + ((k[SDL_SCANCODE_RIGHT] ? 1.5f : 0.f) - (k[SDL_SCANCODE_LEFT] ? 1.5f : 0.f)) * dt;
		fly.pitch = m_mousePitch + ((k[SDL_SCANCODE_UP] ? 1.2f : 0.f) - (k[SDL_SCANCODE_DOWN] ? 1.2f : 0.f)) * dt;
		fly.forward = (k[SDL_SCANCODE_W] ? 1.f : 0.f) - (k[SDL_SCANCODE_S] ? 1.f : 0.f);
		fly.right = (k[SDL_SCANCODE_D] ? 1.f : 0.f) - (k[SDL_SCANCODE_A] ? 1.f : 0.f);
		fly.up = (k[SDL_SCANCODE_E] ? 1.f : 0.f) - (k[SDL_SCANCODE_Q] ? 1.f : 0.f);
		fly.fast = k[SDL_SCANCODE_LSHIFT] != 0;
	}
	m_mouseYaw = m_mousePitch = 0.f;
	return input;
}

void LevelSession::update(float dt, float realDt) {
	m_ambient.update(m_core->scene(), *m_audio, m_core->listenerPosition());
	if (m_core->update(dt, realDt, readInput(dt), m_hasUi)) {
		m_results = true;
		m_hud.startResults();
		m_ui->clearCapture();
		m_needCapture = true;
	}
}

void LevelSession::render(float realDt, bool paused, bool liveBlur) {
	hgr::Camera* cam = m_core->activeCamera();
	const int w = m_app.width, h = m_app.height;
	const float scale = std::max(0.5f, std::min(1.f, Config::get().num("graphics.render_scale", 1.f)));
	const int sw = std::max(1, (int)(w * scale)), sh = std::max(1, (int)(h * scale));
	const bool scaled = scale < 0.99f && m_sceneTarget.begin(sw, sh);
	const int rw = scaled ? sw : w, rh = scaled ? sh : h;
	if (scaled) m_ctx->setDefaultFramebuffer(m_sceneTarget.framebuffer(), sw, sh);
	m_ctx->beginScene();
	m_water->update(cam, rw, rh, m_ctx->time, 0xFF6688AA);
	m_ctx->setViewport(gr::Rect(0, 0, rw, rh));
	m_ctx->clear(m_core->clearColor());
	m_sky.draw(cam, rw, rh);
	cam->render(m_ctx);
	if (scaled) {
		m_ctx->setDefaultFramebuffer(0);
		m_sceneTarget.end(w, h);
		m_ctx->setViewport(gr::Rect(0, 0, w, h));
	}
	if (m_core->followGameCamera()) m_fade.draw(m_core->world().fadeAmount(), m_core->world().fadeColor() == 0);
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
		m_hud.draw(ui, m_core->world(), m_core->player(), paused, m_results, m_menu.isOpen(), realDt);
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
		}
	}
	m_water.reset(new WaterReflection(m_ctx, m_core->scene()));
	if (BallController* player = m_core->player()) {
		WaterReflection* water = m_water.get();
		player->setWaterQuery([water](float x, float z, float& height) { return water->surfaceAt(x, z, height); });
	}
	m_core->setupCamera();
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
		const bool paused = (m_menu.isOpen() && m_core->player() && !m_results) || m_app.overlay->open();
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
