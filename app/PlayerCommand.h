#pragma once

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
	bool autoFollow = false;
};

}
