#include "Hud.h"
#include "BallController.h"
#include "FileUtils.h"
#include "Paths.h"
#include "SceneWorld.h"
#include "Ui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace redbound {

int recordArcadeScore(int meters, std::vector<int>& scores) {
	const std::string dir = userDataDir();
	const std::string path = dir + "/level2.scores";
	scores.clear();
	if (FILE* f = fopen(path.c_str(), "r")) {
		int v;
		while (scores.size() < 3 && fscanf(f, "%d", &v) == 1) scores.push_back(v);
		fclose(f);
	}
	if (scores.empty()) scores = { 3500, 2000, 1000 };
	int rank = -1;
	for (int i = 0; i < (int)scores.size(); ++i)
		if (meters > scores[i]) { rank = i; break; }
	if (rank < 0 && scores.size() < 3) rank = (int)scores.size();
	if (rank >= 0) {
		scores.insert(scores.begin() + rank, meters);
		if (scores.size() > 3) scores.resize(3);
	}
	makeDirectories(dir);
	if (FILE* f = fopen(path.c_str(), "w")) {
		for (int v : scores) fprintf(f, "%d\n", v);
		fclose(f);
	}
	return rank;
}

static int meters(const BallController& player) { return (int)(player.railDistance() / 100.f); }

void Hud::draw(Ui& ui, SceneWorld& world, BallController* player, bool paused, bool results, bool menuOpen, float dt) {
	const int score = world.score();
	if (score != m_lastScore) {
		m_lastScore = score;
		m_showTime = 3.f;
	}
	m_showTime -= dt;
	const float target = (m_showTime > 0.f && !paused) ? 100.f : 0.f;
	m_slide += (target - m_slide) * std::min(1.f, dt * 8.f);
	char text[48];
	if (player && m_slide > 0.5f && m_collectTotal > 0) {
		snprintf(text, sizeof(text), "%d/%d", score, m_collectTotal);
		const int iconW = ui.spriteWidth("INGAME", "LEVEL_1_PICK_ITEM_ICON"), iconH = ui.spriteHeight("INGAME", "LEVEL_1_PICK_ITEM_ICON");
		const int textW = ui.stringWidth("FONT", text);
		const float x = ui.width() + (iconW + textW) * (1.f - m_slide * 0.01f);
		ui.drawSprite("INGAME", "LEVEL_1_PICK_ITEM_ICON", x, 0.f);
		ui.drawString("FONT", text, x - iconW, iconH * 0.5f, 2, 1);
	}
	if (player && player->onRail() && !paused && !results) drawArcade(ui, world, *player);
	if (!results || menuOpen) return;
	m_resultsTime += dt;
	const float alpha = std::min(1.f, m_resultsTime * 2.f);
	if (player && player->onRail()) drawArcadeResults(ui, *player, alpha);
	if (m_collectTotal > 0) {
		snprintf(text, sizeof(text), "%d/%d", score, m_collectTotal);
		ui.drawSprite("INGAME", "LEVEL_1_RESULTS_BG", ui.width() * 0.5f, 240.f, alpha);
		ui.drawString(score >= m_collectTotal ? "FONT_YELLOW" : "FONT", text, ui.width() * 0.5f, 310.f, 1, 1, alpha);
	}
}

void Hud::drawArcade(Ui& ui, SceneWorld& world, BallController& player) {
	char text[48];
	const float width = (float)ui.width();
	ui.drawSprite("INGAME", "LEVEL_2_HUD_TIME", 0.f, 0.f);
	snprintf(text, sizeof(text), "%.1f", world.getVariable("timer01"));
	ui.drawString("FONT", text, 175.f, 25.f, 2, 1);
	ui.drawSprite("INGAME", "LEVEL_2_HUD_DISTANCE", width, 0.f);
	snprintf(text, sizeof(text), "%d", meters(player));
	ui.drawString("FONT", text, width - 52.f, 25.f, 2, 1);
	const int countdown = (int)(world.getVariable("countdown") + 0.5f);
	if (countdown >= 1 && countdown <= 3) {
		snprintf(text, sizeof(text), "LEVEL_2_START_COUNTDOWN_%d", countdown);
		ui.drawSprite("INGAME", text, width * 0.5f, 160.f);
	}
}

void Hud::drawArcadeResults(Ui& ui, BallController& player, float alpha) {
	if (m_arcadeRank == -2) m_arcadeRank = recordArcadeScore(meters(player), m_arcadeScores);
	const float cx = ui.width() * 0.5f, top = 82.f;
	char text[48];
	ui.drawSprite("INGAME", "LEVEL_2_RESULTS_BG", cx, 229.f, alpha);
	snprintf(text, sizeof(text), "%d m", meters(player));
	ui.drawString("FONT", text, cx, top + 66.f, 1, 1, alpha);
	for (int r = 0; r < (int)m_arcadeScores.size() && r < 3; ++r) {
		const char* font = r == m_arcadeRank ? "FONT_YELLOW" : "FONT";
		snprintf(text, sizeof(text), "%d.", r + 1);
		ui.drawString(font, text, cx - 100.f, top + 190.f + r * 30.f, 0, 1, alpha);
		snprintf(text, sizeof(text), "%d m", m_arcadeScores[r]);
		ui.drawString(font, text, cx + 115.f, top + 190.f + r * 30.f, 2, 1, alpha);
	}
}

void Hud::drawLogo(Ui& ui, bool paused, float dt) {
	if (m_logoTime < 0.f || paused) return;
	m_logoTime += dt;
	const float alpha = m_logoTime < 2.5f ? std::min(1.f, m_logoTime * 3.f) : std::max(0.f, 1.f - (m_logoTime - 2.5f) / 1.5f);
	ui.drawSprite("MENU", "MENU_LOGO", ui.width() * 0.5f, 240.f, alpha);
	if (m_logoTime > 4.f) m_logoTime = -1.f;
}

}
