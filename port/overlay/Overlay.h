#pragma once
#include <SDL.h>
#include <vector>
#include <functional>
#include <string>

namespace redbound {

class Audio;

void applyAudioSettings(Audio& audio);

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
	bool m_icons = false;
	bool m_screenshotRequested = false;
	bool m_confirmClear = false;
	void drawMenu();
	void dragScroll();
	bool m_scrollCandidate = false, m_scrollDragging = false;
	float m_scrollVelocity = 0.f;
	void drawLabels();
	void drawNotices();
	struct Notice {
		std::string text, icon;
		float age = 0.f, life = 3.5f;
	};
	std::vector<Notice> m_notices;
	Uint32 m_lastNoticeTicks = 0;
	bool m_updateNotified = false;
	struct Label {
		std::string text;
		float rightPx = 0.f, centerYPx = 0.f, heightPx = 0.f;
	};
	std::vector<Label> m_labels;
public:
	void addLabel(const std::string& text, float rightPx, float centerYPx, float heightPx) { m_labels.push_back({ text, rightPx, centerYPx, heightPx }); }
	void notify(const std::string& text, const char* glyph = nullptr, float seconds = 3.5f);
	float labelWidth(const std::string& text, float heightPx) const;
private:
	std::string saveScreenshot();
	int m_titleW = 0, m_titleH = 0;
	float m_scale = 1.f;
	bool m_compact = false;
	void layout();
	int m_lastW = 0, m_lastH = 0;
};

}
