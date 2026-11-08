#pragma once
#include "redbound/Trigger.h"
#include "redbound/World.h"
#include <hgr/Scene.h>
#include <hgr/Node.h>
#include <hgr/TransformAnimation.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace redbound {

class SceneWorld : public World {
public:
	explicit SceneWorld(hgr::Scene* scene);

	void update(float dt);

	void registerNode(const std::string& name, hgr::Node* n) { m_nodes[name] = n; }
	std::vector<std::string> addTrigger(const std::string& name, const std::string& properties);
	int triggerState(const std::string& name) const;
	std::function<void(const std::string& prs, hgr::Node* at)> spawnParticles;
	std::function<void(float)> onPressure;
	std::function<void(const std::string& file, bool loop)> playSound;
	std::function<void(const std::string& file)> stopSound;
	int score() const { return (int)getVariable("score"); }
	int gameMode() const { return m_gameMode; }
	bool levelCompleted() const { return m_levelComplete; }

	bool takeRespawn() { const bool r = m_respawn; m_respawn = false; return r; }
	bool hasVariable(const std::string& n) const { return m_vars.count(n) != 0; }
	bool takeUpdateBallValues() { const bool r = m_updateBall; m_updateBall = false; return r; }
	struct BallRequest {
		enum Kind { ATTACH_RAIL, DETACH_RAIL, SPEED_MUL, MAX_SPEED, SET_POS, SET_GUIDE } kind;
		std::string name;
		float value = 0.f;
	};
	std::vector<BallRequest> takeBallRequests() { std::vector<BallRequest> r; r.swap(m_ballRequests); return r; }
	float cameraViewA() const { return m_camViewA; }
	float cameraViewB() const { return m_camViewB; }
	bool hasCameraView() const { return m_camViewSet; }
	bool takeResetBallValues() { const bool r = m_resetBall; m_resetBall = false; return r; }
	const std::string& checkpoint() const { return m_checkpoint; }
	hgr::Node* findNode(const std::string& name) const { return node(name); }
	const std::map<std::string, std::unique_ptr<Trigger>>& triggers() const { return m_triggers; }

	const std::string& activeCamera() const { return m_activeCamera; }
	void setKeyDown(const std::string& key, bool down) { if (down) m_keys.insert(key); else m_keys.erase(key); }
	size_t triggerCount() const { return m_triggers.size(); }
	const std::vector<std::string>& warnings() const { return m_warnings; }
	std::vector<std::string> runningTriggers() const;
	const std::map<std::string, P(hgr::Node)>& nodes() const { return m_nodes; }
	bool isLinked(hgr::Node* n) const {
		for (hgr::Node* p = n; p; p = p->parent()) if (p == (hgr::Node*)m_scene) return true;
		return false;
	}

	float getVariable(const std::string& name) const override;
	void setVariable(const std::string& name, float value) override { m_vars[name] = value; }
	void setPersistentVariable(const std::string& name, float value) override { m_vars[name] = value; m_persistent[name] = value; }
	void setTriggerEnabled(const std::string& trigger, bool enabled) override;
	void activateTrigger(const std::string& trigger) override;
	void link(const std::string& parent, const std::vector<std::string>& nodes) override;
	void unlink(const std::vector<std::string>& nodes) override;
	int playAnimation(const std::string& node, const std::string& anim, float start, float end, bool loop) override;
	void addAnimationSet(hgr::TransformAnimationSet* set) { if (set) m_animationSets.push_back(set); }
	bool animationDone(int id) const override;
	int startTransform(TransformKind kind, const std::string& object, const std::string& subnode,
	                   float x, float y, float z, float duration) override;
	bool transformDone(int id) const override;
	void setFade(float amount, int color) override { m_fade = amount; m_fadeColor = color; }
	bool isKeyDown(const std::string& key) const override { return m_keys.count(key) != 0; }

	float fadeAmount() const { return m_fade; }
	int fadeColor() const { return m_fadeColor; }
	bool genericAction(const std::string& name, const std::vector<std::string>& args) override;

private:
	struct Playing {
		P(hgr::Node) node;
		P(hgr::TransformAnimation) anim;
		float time, start, end;
		bool loop, done;
	};

	struct Moving {
		TransformKind kind;
		P(hgr::Node) node;
		math::float3 rate;
		float elapsed, duration;
		bool done;
		float u, v;
	};

	P(hgr::Scene) m_scene;
	std::map<int, Moving> m_moving;
	std::map<hgr::Node*, std::pair<float, float> > m_uvOffset;
	float m_fade = 0.f;
	int m_fadeColor = 0;
	std::vector<hgr::TransformAnimationSet*> m_animationSets;
	std::map<std::string, P(hgr::Node)> m_nodes;
	std::map<std::string, std::unique_ptr<Trigger>> m_triggers;
	std::map<std::string, float> m_vars, m_persistent;
	std::map<int, Playing> m_playing;
	int m_nextAnim = 0;
	std::set<std::string> m_keys;
	std::string m_activeCamera;
	std::vector<std::string> m_warnings;
	std::set<std::string> m_unhandled;
	std::string m_checkpoint;
	std::string m_currentTrigger;
	int m_activateDepth = 0;
	bool m_respawn = false;
	bool m_updateBall = false, m_resetBall = false;
	std::vector<BallRequest> m_ballRequests;
	float resolve(const std::string& arg) const;
	float m_camViewA = 0.f, m_camViewB = 0.f;
	bool m_camViewSet = false;
	int m_gameMode = 0;
	bool m_levelComplete = false;

	hgr::Node* node(const std::string& name) const;
};

}
