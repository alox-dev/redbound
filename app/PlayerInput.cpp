#include "PlayerInput.h"
#include "Config.h"
#include "Devices.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace redbound {

static float clampUnit(float v) { return std::max(-1.f, std::min(1.f, v)); }

std::string PlayerInput::touchScheme() { return Config::get().flag("controls.accelerometer", false) ? "accelerometer" : "gestures"; }

void PlayerInput::registerTap(const Finger& f, float x, float y, int width, int height) {
	const float moved = std::hypot((x - f.startX) * width, (y - f.startY) * height);
	const Uint32 now = SDL_GetTicks();
	if (now - f.startTime >= 250 || moved >= height * 0.04f) return;
	if (m_lastTap && now - m_lastTap < 350) {
		m_jumpFrames = 6;
		m_lastTap = 0;
	} else {
		m_lastTap = now;
	}
}

static bool fingerStillDown(SDL_FingerID id) {
	for (int d = 0; d < SDL_GetNumTouchDevices(); ++d) {
		const SDL_TouchID touch = SDL_GetTouchDevice(d);
		for (int i = 0; i < SDL_GetNumTouchFingers(touch); ++i)
			if (SDL_Finger* f = SDL_GetTouchFinger(touch, i))
				if (f->id == id) return true;
	}
	return false;
}

void PlayerInput::resetTouch() {
	m_move.active = m_look.active = false;
	m_lookPendingX = m_lookPendingY = 0.f;
	m_holdYawSpeed = 0.f;
	m_flickTime = 0.f;
}

void PlayerInput::handleTouch(const SDL_Event& e, int width, int height) {
	if (touchScheme() != "gestures") return;
	if (e.type == SDL_FINGERDOWN) {
		if (e.tfinger.x < 0.12f && e.tfinger.y < 0.15f) return;
		Finger* f = !m_move.active ? &m_move : !m_look.active ? &m_look : nullptr;
		if (!f) return;
		f->active = true;
		f->swiping = false;
		f->id = e.tfinger.fingerId;
		f->startX = f->x = f->restX = e.tfinger.x;
		f->startY = f->y = f->restY = e.tfinger.y;
		f->startTime = f->restTime = SDL_GetTicks();
	} else if (e.type == SDL_FINGERMOTION) {
		Finger* f = m_move.active && e.tfinger.fingerId == m_move.id ? &m_move : m_look.active && e.tfinger.fingerId == m_look.id ? &m_look : nullptr;
		if (!f) return;
		if (f == &m_look) {
			m_lookPendingX += e.tfinger.x - f->x;
			m_lookPendingY += e.tfinger.y - f->y;
			m_lastLookTime = SDL_GetTicks();
		}
		f->x = e.tfinger.x;
		f->y = e.tfinger.y;
		if (std::hypot((f->x - f->startX) * width, (f->y - f->startY) * height) > height * 0.06f) f->swiping = true;
		if (std::hypot((f->x - f->restX) * width, (f->y - f->restY) * height) > height * 0.025f) {
			f->restX = f->x;
			f->restY = f->y;
			f->restTime = SDL_GetTicks();
		}
	} else if (e.type == SDL_FINGERUP) {
		if (m_move.active && e.tfinger.fingerId == m_move.id) {
			m_move.active = false;
			registerTap(m_move, e.tfinger.x, e.tfinger.y, width, height);
			const float dx = (e.tfinger.x - m_move.startX) * width, dy = (e.tfinger.y - m_move.startY) * height;
			const float moved = std::hypot(dx, dy);
			if (moved > height * 0.06f && SDL_GetTicks() - m_move.startTime < 220) {
				m_flickTime = 0.35f;
				m_flickX = dx / moved;
				m_flickY = -dy / moved;
			}
			if (m_look.active) {
				m_move = m_look;
				m_move.startX = m_move.x;
				m_move.startY = m_move.y;
				m_move.swiping = false;
				m_look.active = false;
			}
		} else if (m_look.active && e.tfinger.fingerId == m_look.id) {
			m_look.active = false;
			registerTap(m_look, e.tfinger.x, e.tfinger.y, width, height);
		}
	}
}

bool PlayerInput::anyJumpButton() const {
	for (SDL_GameController* pad : Devices::instance().gamepads())
		if (SDL_GameControllerGetAttached(pad) && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) return true;
	return false;
}

void PlayerInput::readTilt(float dt, float& tiltX, float& tiltY) {
	Devices& devices = Devices::instance();
	float a[3];
	if (!devices.accelerometer() || SDL_SensorGetData(devices.accelerometer(), a, 3) != 0) return;
	for (int c = 0; c < 3; ++c) m_gravity[c] = m_gravity[c] * 0.85f + a[c] * 0.15f;
	if (!m_hasNeutral || devices.recalibrationRequested()) {
		if (m_gravity[0] * m_gravity[0] + m_gravity[1] * m_gravity[1] + m_gravity[2] * m_gravity[2] > 25.f) {
			for (int c = 0; c < 3; ++c) m_neutral[c] = m_gravity[c];
			m_hasNeutral = true;
			devices.clearRecalibration();
		}
	}
	const float sign = SDL_GetDisplayOrientation(0) == SDL_ORIENTATION_LANDSCAPE_FLIPPED ? -1.f : 1.f;
	auto right = [&](const float* v) { return -sign * v[1]; };
	auto up = [&](const float* v) { return sign * v[0]; };
	Config& cfg = Config::get();
	const float g = 9.81f, sensitivity = cfg.num("controls.tilt_sensitivity", 1.f) / 0.34f;
	float tx = -(right(m_gravity) - right(m_neutral)) / g * sensitivity;
	float ty = -(up(m_gravity) - up(m_neutral)) / g * sensitivity;
	if (cfg.flag("controls.tilt_invert_x", false)) tx = -tx;
	if (cfg.flag("controls.tilt_invert_y", false)) ty = -ty;
	tx = clampUnit(tx);
	ty = clampUnit(ty);
	if (std::fabs(tx) > 0.14f) tiltX = (tx - (tx > 0.f ? 0.14f : -0.14f)) / 0.86f;
	if (std::fabs(ty) > 0.08f) tiltY = (ty - (ty > 0.f ? 0.08f : -0.08f)) / 0.92f;
	m_shakeCooldown = std::max(0.f, m_shakeCooldown - dt);
	const float magnitude = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	if (m_shakeCooldown <= 0.f && std::fabs(magnitude - g) > 7.f) {
		m_jumpFrames = 6;
		m_shakeCooldown = 0.5f;
	}
}

PlayerCommand PlayerInput::update(float dt, int width, int height, bool controllable) {
	Config& cfg = Config::get();
	Devices& devices = Devices::instance();
	const Uint8* k = SDL_GetKeyboardState(0);
	PlayerCommand cmd;
	float forward = 0.f, turn = 0.f;
	if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_KP_8] || k[SDL_SCANCODE_8]) forward += 1.f;
	if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_KP_2] || k[SDL_SCANCODE_2]) forward -= 1.f;
	if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_KP_6] || k[SDL_SCANCODE_6]) turn += 1.f;
	if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_KP_4] || k[SDL_SCANCODE_4]) turn -= 1.f;
	const bool keyboard = forward != 0.f || turn != 0.f;
	bool jump = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_5] || k[SDL_SCANCODE_KP_0] || k[SDL_SCANCODE_5] || k[SDL_SCANCODE_0];
	const float deadzone = cfg.num("controls.deadzone", 0.22f);
	float lx = 0.f, ly = 0.f, rx = 0.f, ry = 0.f;
	bool dpad = false;
	for (SDL_GameController* pad : devices.gamepads()) {
		if (!SDL_GameControllerGetAttached(pad)) continue;
		auto axis = [&](SDL_GameControllerAxis a) {
			const float v = SDL_GameControllerGetAxis(pad, a) / 32767.f;
			return std::fabs(v) < deadzone ? 0.f : v;
		};
		if (std::fabs(axis(SDL_CONTROLLER_AXIS_LEFTX)) + std::fabs(axis(SDL_CONTROLLER_AXIS_LEFTY)) > std::fabs(lx) + std::fabs(ly)) {
			lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
			ly = -axis(SDL_CONTROLLER_AXIS_LEFTY);
		}
		if (std::fabs(axis(SDL_CONTROLLER_AXIS_RIGHTX)) + std::fabs(axis(SDL_CONTROLLER_AXIS_RIGHTY)) > std::fabs(rx) + std::fabs(ry)) {
			rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
			ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
		}
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) { forward += 1.f; dpad = true; }
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) { forward -= 1.f; dpad = true; }
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) { turn += 1.f; dpad = true; }
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) { turn -= 1.f; dpad = true; }
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) jump = true;
	}
	for (SDL_Joystick* js : devices.joysticks()) {
		if (!SDL_JoystickGetAttached(js)) continue;
		auto axis = [&](int a) {
			if (a >= SDL_JoystickNumAxes(js)) return 0.f;
			const float v = SDL_JoystickGetAxis(js, a) / 32767.f;
			return std::fabs(v) < deadzone ? 0.f : v;
		};
		if (lx == 0.f && ly == 0.f) {
			lx = axis(0);
			ly = -axis(1);
		}
		const Uint8 hat = SDL_JoystickNumHats(js) > 0 ? SDL_JoystickGetHat(js, 0) : 0;
		if (hat & SDL_HAT_UP) { forward += 1.f; dpad = true; }
		if (hat & SDL_HAT_DOWN) { forward -= 1.f; dpad = true; }
		if (hat & SDL_HAT_RIGHT) { turn += 1.f; dpad = true; }
		if (hat & SDL_HAT_LEFT) { turn -= 1.f; dpad = true; }
		for (int b = 0; b < std::min(2, SDL_JoystickNumButtons(js)); ++b)
			if (SDL_JoystickGetButton(js, b)) jump = true;
	}
	if (touchScheme() != "gestures") resetTouch();
	if (SDL_GetNumTouchDevices() > 0) {
		if (m_move.active && !fingerStillDown(m_move.id)) m_move.active = false;
		if (m_look.active && !fingerStillDown(m_look.id)) m_look.active = false;
	}
	float touchX = 0.f, touchY = 0.f, holdTarget = 0.f;
	bool touchActive = m_look.active;
	if (m_move.active) {
		touchActive = true;
		if (m_move.swiping) {
			const float radius = height * 0.08f;
			touchX = clampUnit((m_move.x - m_move.startX) * width / radius);
			touchY = clampUnit(-(m_move.y - m_move.startY) * height / radius);
			if (std::fabs(touchX) < 0.12f) touchX = 0.f;
			if (std::fabs(touchY) < 0.12f) touchY = 0.f;
		} else if (SDL_GetTicks() - m_move.restTime > 200) {
			holdTarget = m_move.restX > 0.5f ? 1.f : -1.f;
			m_lastLookTime = SDL_GetTicks();
		}
	}
	m_holdYawSpeed += (holdTarget * 1.8f - m_holdYawSpeed) * std::min(1.f, dt * 10.f);
	if (holdTarget == 0.f && std::fabs(m_holdYawSpeed) < 0.01f) m_holdYawSpeed = 0.f;
	const float lookBlend = std::min(1.f, dt * 18.f);
	const float lookX = m_lookPendingX * lookBlend, lookY = m_lookPendingY * lookBlend;
	m_lookPendingX -= lookX;
	m_lookPendingY -= lookY;
	const float lookSensitivity = cfg.num("controls.camera_sensitivity", 1.f);
	const float aspect = height > 0 ? (float)width / (float)height : 1.f;
	const float lookYaw = (lookX * 3.14159265f + m_holdYawSpeed * dt) * lookSensitivity;
	const float lookPitch = lookY * 1.6f / std::max(1.f, aspect * 0.5f) * lookSensitivity * (cfg.flag("controls.invert_y", false) ? -1.f : 1.f);
	if (m_flickTime > 0.f) {
		m_flickTime -= dt;
		touchX = m_flickX;
		touchY = m_flickY;
		touchActive = true;
	}
	if (m_jumpFrames > 0) {
		jump = true;
		--m_jumpFrames;
	}
	if (controllable && !m_wasControllable) devices.requestRecalibration();
	m_wasControllable = controllable;
	const bool tilt = touchScheme() == "accelerometer" && devices.accelerometer();
	if (tilt) {
		readTilt(dt, touchX, touchY);
		touchActive = true;
	}
	forward = clampUnit(forward);
	turn = clampUnit(turn);
	const bool sticks = lx != 0.f || ly != 0.f || rx != 0.f || ry != 0.f;
	const bool mouseOrbit = m_orbitX != 0.f || m_orbitY != 0.f;
	if (touchActive || mouseOrbit || (m_mouseCaptured && keyboard)) m_modern = true;
	else if (dpad || keyboard) m_modern = false;
	else if (sticks) m_modern = true;
	const float sensitivity = cfg.num("controls.camera_sensitivity", 1.f);
	const float invertY = cfg.flag("controls.invert_y", false) ? -1.f : 1.f;
	cmd.cameraYaw = (rx * 2.6f * dt + m_orbitX * 0.005f) * sensitivity + lookYaw;
	cmd.cameraPitch = (ry * 1.8f * dt + m_orbitY * 0.005f) * sensitivity * invertY + lookPitch;
	m_orbitX = m_orbitY = 0.f;
	const bool leftStick = lx != 0.f || ly != 0.f;
	cmd.moveX = leftStick ? lx : turn;
	cmd.moveY = leftStick ? ly : forward;
	if (touchActive && (touchX != 0.f || touchY != 0.f)) {
		cmd.moveX = touchX;
		cmd.moveY = touchY;
	}
	if (!m_modern && touchActive) {
		turn += touchX;
		forward += touchY;
	}
	if (!m_modern && leftStick) {
		turn = lx;
		forward = ly;
	}
	cmd.forward = forward;
	cmd.turn = turn;
	cmd.jump = jump;
	cmd.modern = m_modern;
	cmd.followPath = tilt && cfg.str("controls.tilt_camera", "follow") == "follow";
	cmd.autoFollow = m_move.active && m_move.swiping && SDL_GetTicks() - m_lastLookTime > 1000;
	return cmd;
}

}
