#pragma once
#include <vector>

namespace redbound {

class Ui;
class SceneWorld;
class BallController;

class Hud {
public:
	void setCollectableTotal(int total) { m_collectTotal = total; }
	int collectableTotal() const { return m_collectTotal; }
	void startLogo() { m_logoTime = 0.f; }
	void startResults() { m_resultsTime = 0.f; }
	void draw(Ui& ui, SceneWorld& world, BallController* player, bool paused, bool results, bool menuOpen, float dt);
	void drawLogo(Ui& ui, bool paused, float dt);
private:
	int m_collectTotal = 0;
	int m_lastScore = 0;
	float m_showTime = 0.f;
	float m_slide = 0.f;
	float m_resultsTime = 0.f;
	float m_logoTime = -1.f;
	int m_arcadeRank = -2;
	std::vector<int> m_arcadeScores;
	void drawArcade(Ui& ui, SceneWorld& world, BallController& player);
	void drawArcadeResults(Ui& ui, BallController& player, float alpha);
};


}
