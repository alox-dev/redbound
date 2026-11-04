#pragma once
#include <SDL.h>
#include <string>

namespace redbound {

struct PlayerCommand {
	float forward = 0.f;
	float turn = 0.f;
	bool jump = false;
	bool modern = false;
	float moveX = 0.f;
	float moveY = 0.f;
	float cameraYaw = 0.f;
	float cameraPitch = 0.f;
	bool followPath = false;
};

class PlayerInput {
public:
	static std::string touchScheme();
	void setMouseCaptured(bool captured) { m_mouseCaptured = captured; }
	void handleTouch(const SDL_Event& e, int width, int height);
	void addMouseOrbit(float dx, float dy) { m_orbitX += dx; m_orbitY += dy; }
	PlayerCommand update(float dt, int width, int height, bool controllable);
	bool anyJumpButton() const;
private:
	struct Finger {
		bool active = false;
		SDL_FingerID id = 0;
		bool swiping = false;
		float startX = 0, startY = 0, x = 0, y = 0, restX = 0, restY = 0;
		Uint32 startTime = 0, restTime = 0;
	};
	Finger m_move;
	Finger m_look;
	float m_lookRateX = 0.f, m_lookRateY = 0.f, m_holdYawSpeed = 0.f;
	bool m_mouseCaptured = false;
	Uint32 m_lastTap = 0;
	int m_jumpFrames = 0;
	float m_flickTime = 0.f, m_flickX = 0.f, m_flickY = 0.f;
	float m_orbitX = 0.f, m_orbitY = 0.f;
	float m_gravity[3] = { 0, 0, 0 };
	float m_neutral[3] = { 0, 0, 0 };
	bool m_hasNeutral = false;
	float m_shakeCooldown = 0.f;
	bool m_wasControllable = false;
	bool m_modern = false;
	void readTilt(float dt, float& tiltX, float& tiltY);
	void registerTap(const Finger& f, float x, float y, int width, int height);
};

}
