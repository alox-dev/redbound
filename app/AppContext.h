#pragma once
#include <SDL.h>

namespace redbound {

class Overlay;

struct AppContext {
	SDL_Window* window = 0;
	int width = 800;
	int height = 480;
	Overlay* overlay = 0;
};

}
