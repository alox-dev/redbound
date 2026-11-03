#include "LevelMenu.h"
#include "Ui.h"

namespace redbound {

static const int CARD_FIRST = 4;

void LevelMenu::open() {
	m_page = 0;
	m_hover = -1;
}

void LevelMenu::close() {
	m_page = -1;
	m_closedAt = SDL_GetTicks();
}

void LevelMenu::layout(float width) {
	m_buttons.clear();
	m_buttons.push_back({ "MENU_TASK_SWITCHER", 0.f, 0.f, MenuAction::Minimize });
	m_buttons.push_back({ m_volumeOn ? "MENU_VOLUME_ON" : "MENU_VOLUME_OFF", 0.f, 0.f, MenuAction::ToggleVolume });
	m_buttons.push_back({ m_page == 0 ? "MENU_HELP" : "", 0.f, 0.f, m_page == 0 ? MenuAction::Credits : MenuAction::None });
	m_buttons.push_back({ "MENU_EXIT", width, 0.f, MenuAction::Quit });
	if (m_page == 0) {
		const float cy = 243.f, gap = 256.f;
		m_buttons.push_back({ "MENU_LEVEL_ATTRACTION", width * 0.5f - gap, cy, MenuAction::Attraction });
		m_buttons.push_back({ "MENU_LEVEL_1", width * 0.5f, cy, MenuAction::Level1 });
		m_buttons.push_back({ "MENU_LEVEL_2", width * 0.5f + gap, cy, MenuAction::Level2 });
	}
}

MenuAction LevelMenu::activate(int index, bool& resumed) {
	if (index < 0 || index >= (int)m_buttons.size()) return MenuAction::None;
	const MenuAction action = m_buttons[index].action;
	if (action == MenuAction::Credits) {
		m_page = 1;
		m_hover = -1;
	} else if (action == MenuAction::ToggleVolume) {
		m_volumeOn = !m_volumeOn;
	}
	(void)resumed;
	return action;
}

MenuAction LevelMenu::handleEvent(const SDL_Event& e, Ui& ui, int height, bool& resumed) {
	resumed = false;
	if (!isOpen()) return MenuAction::None;
	layout((float)ui.width());
	const float scale = height / 480.f;
	const bool ghostTouch = e.type == SDL_MOUSEBUTTONDOWN && e.button.which == SDL_TOUCH_MOUSEID && SDL_GetTicks() < m_ignoreTouchUntil;
	if (!ghostTouch && (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN)) {
		const float mx = (e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x) / scale;
		const float my = (e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y) / scale;
		m_hover = -1;
		for (size_t i = 0; i < m_buttons.size(); ++i)
			if (!m_buttons[i].sprite.empty() && ui.hitSprite("MENU", m_buttons[i].sprite, m_buttons[i].x, m_buttons[i].y, 1.f, mx, my)) m_hover = (int)i;
		if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
			if (m_page == 1 && m_hover < 0) m_page = 0;
			else if (m_page == 0 && m_hover < 0) { close(); resumed = true; }
			else return activate(m_hover, resumed);
		}
		return MenuAction::None;
	}
	if (e.type == SDL_KEYDOWN) {
		const SDL_Keycode key = e.key.keysym.sym;
		if (key == SDLK_p || key == SDLK_ESCAPE) {
			if (m_page == 1) m_page = 0;
			else { close(); resumed = true; }
		} else if (m_page == 0 && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
			const int card = m_hover >= CARD_FIRST ? m_hover - CARD_FIRST : -1;
			m_hover = CARD_FIRST + (key == SDLK_LEFT ? (card < 0 ? 2 : (card + 2) % 3) : (card < 0 ? 0 : (card + 1) % 3));
		} else if (key == SDLK_RETURN || key == SDLK_SPACE) {
			return activate(m_hover, resumed);
		}
	}
	return MenuAction::None;
}

void LevelMenu::draw(Ui& ui) {
	if (!isOpen()) return;
	const float width = (float)ui.width();
	layout(width);
	if (m_page == 1) ui.drawSprite("MENU", "MENU_CREDITS", (width - 800.f) * 0.5f, 0.f);
	for (size_t i = 0; i < m_buttons.size(); ++i) {
		const Button& b = m_buttons[i];
		if (b.sprite.empty()) continue;
		ui.drawSprite("MENU", b.sprite, b.x, b.y, 1.f, (int)i == m_hover ? 1.06f : 1.f);
	}
}

}
