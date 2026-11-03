#pragma once
#include <SDL.h>
#include <vector>

namespace redbound {

class Devices {
public:
	static Devices& instance();
	void init();
	void openAll();
	bool handleEvent(const SDL_Event& e);
	const std::vector<SDL_GameController*>& gamepads() const { return m_gamepads; }
	const std::vector<SDL_Joystick*>& joysticks() const { return m_joysticks; }
	SDL_Sensor* accelerometer() const { return m_accelerometer; }
	void rumble(float strength);
	void requestRecalibration() { m_recalibrate = true; }
	bool recalibrationRequested() const { return m_recalibrate; }
	void clearRecalibration() { m_recalibrate = false; }
private:
	std::vector<SDL_GameController*> m_gamepads;
	std::vector<SDL_Joystick*> m_joysticks;
	SDL_Sensor* m_accelerometer = 0;
	SDL_Haptic* m_haptic = 0;
	bool m_recalibrate = false;
	void loadMappings();
	void openSensors();
	void closeDetached();
};

}
