#pragma once
#include <SDL.h>
#include <functional>
#include <string>

namespace redbound {

class Audio;

class Overlay {
public:
	bool init(SDL_Window* win, SDL_GLContext ctx);
	void shutdown();
	bool processEvent(const SDL_Event& e);
	void draw();
	bool open() const { return m_open; }
	void setOpen(bool o) { m_open = o; }
	void setImportMode(bool m) { m_importMode = m; if (m) m_open = true; }
	void setAudio(Audio* a) { m_audio = a; }
	std::function<void()> onQuit;
	std::function<void()> onCalibrate;
private:
	SDL_Window* m_win = 0;
	Audio* m_audio = 0;
	bool m_ready = false, m_open = false;
	std::string m_status;
	bool m_importMode = false, m_waitPick = false;
	float m_scanTimer = 0.f;
	void importFrom(const std::string& deb);
	void drawGame();
	void drawControls();
	void drawGraphics();
	void drawAudio();
	void drawImprovements();
	void drawLanguage();
	void drawAbout();
	void drawImport();
	void drawImportButton(bool ok);
	void loadTitle();
	void drawTitle(float maxWidth);
	unsigned int m_titleTex = 0;
	int m_titleW = 0, m_titleH = 0;
	float m_scale = 1.f;
	bool m_compact = false;
	void layout();
	int m_lastW = 0, m_lastH = 0;
};

}
