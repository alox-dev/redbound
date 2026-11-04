#include "Devices.h"
#include "Config.h"
#include "Paths.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace redbound {

Devices& Devices::instance() {
	static Devices d;
	return d;
}

void Devices::init() {
	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
	SDL_InitSubSystem(SDL_INIT_SENSOR);
	SDL_InitSubSystem(SDL_INIT_HAPTIC);
	loadMappings();
	openSensors();
}

void Devices::loadMappings() {
	int count = -1;
#ifdef __ANDROID__
	count = SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
#else
	if (char* base = SDL_GetBasePath()) {
		count = SDL_GameControllerAddMappingsFromFile((std::string(base) + "gamecontrollerdb.txt").c_str());
		SDL_free(base);
	}
#ifdef REDBOUND_CONTROLLERDB
	if (count <= 0) count = SDL_GameControllerAddMappingsFromFile(REDBOUND_CONTROLLERDB);
#endif
	const int user = SDL_GameControllerAddMappingsFromFile((userConfigDir() + "/gamecontrollerdb.txt").c_str());
	if (user > 0) count = (count > 0 ? count : 0) + user;
#endif
	printf("controllers: %d mappings from SDL_GameControllerDB\n", count);
}

void Devices::openSensors() {
	for (int i = 0; i < SDL_NumSensors() && !m_accelerometer; ++i)
		if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_ACCEL) m_accelerometer = SDL_SensorOpen(i);
	if (SDL_NumHaptics() > 0 && (m_haptic = SDL_HapticOpen(0)) != 0 && SDL_HapticRumbleInit(m_haptic) != 0) {
		SDL_HapticClose(m_haptic);
		m_haptic = 0;
	}
	printf("sensors: accelerometer %s, vibration %s\n", m_accelerometer ? "yes" : "no", m_haptic ? "yes" : "no");
}

void Devices::openAll() {
	for (int i = 0; i < SDL_NumJoysticks(); ++i) {
		if (SDL_IsGameController(i)) {
			SDL_GameController* pad = SDL_GameControllerOpen(i);
			if (pad && std::find(m_gamepads.begin(), m_gamepads.end(), pad) == m_gamepads.end()) {
				m_gamepads.push_back(pad);
				printf("controller: %s\n", SDL_GameControllerName(pad));
			}
		} else {
			SDL_Joystick* js = SDL_JoystickOpen(i);
			if (js && std::find(m_joysticks.begin(), m_joysticks.end(), js) == m_joysticks.end()) {
				m_joysticks.push_back(js);
				printf("generic controller (no known mapping): %s, %d axes, %d buttons\n", SDL_JoystickName(js),
				       SDL_JoystickNumAxes(js), SDL_JoystickNumButtons(js));
			}
		}
	}
}

void Devices::closeDetached() {
	for (size_t i = 0; i < m_gamepads.size();) {
		if (!SDL_GameControllerGetAttached(m_gamepads[i])) {
			SDL_GameControllerClose(m_gamepads[i]);
			m_gamepads.erase(m_gamepads.begin() + i);
		} else ++i;
	}
	for (size_t i = 0; i < m_joysticks.size();) {
		if (!SDL_JoystickGetAttached(m_joysticks[i])) {
			SDL_JoystickClose(m_joysticks[i]);
			m_joysticks.erase(m_joysticks.begin() + i);
		} else ++i;
	}
}

bool Devices::handleEvent(const SDL_Event& e) {
	if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_JOYDEVICEADDED) {
		openAll();
		return true;
	}
	if (e.type == SDL_CONTROLLERDEVICEREMOVED || e.type == SDL_JOYDEVICEREMOVED) {
		closeDetached();
		return true;
	}
	return false;
}

void Devices::rumble(float strength) {
	if (!Config::get().flag("controls.vibration", true)) return;
	const Uint32 ms = (Uint32)std::max(30.f, std::min(250.f, 45.f * strength));
	if (m_haptic) SDL_HapticRumblePlay(m_haptic, 0.6f, ms);
	for (SDL_GameController* pad : m_gamepads) SDL_GameControllerRumble(pad, 0x6000, 0xA000, ms);
}

}
