#pragma once
#include <SDL.h>
#include <string>
#include <vector>

namespace redbound {

class Ui;

enum class MenuAction { None, Attraction, Level1, Level2, Credits, ToggleVolume, Minimize, Quit };

class LevelMenu {
public:
	bool isOpen() const { return m_page >= 0; }
	void open();
	void close();
	MenuAction handleEvent(const SDL_Event& e, Ui& ui, int height, bool& resumed);
	void draw(Ui& ui);
	bool volumeOn() const { return m_volumeOn; }
	void setVolumeOn(bool on) { m_volumeOn = on; }
	void ignoreTouchClicksUntil(Uint32 ticks) { m_ignoreTouchUntil = ticks; }
	Uint32 closedAt() const { return m_closedAt; }
private:
	struct Button {
		std::string sprite;
		float x, y;
		MenuAction action;
	};
	int m_page = -1;
	int m_hover = -1;
	bool m_volumeOn = true;
	Uint32 m_ignoreTouchUntil = 0;
	Uint32 m_closedAt = 0;
	std::vector<Button> m_buttons;
	void layout(float width);
	MenuAction activate(int index, bool& resumed);
};

}
