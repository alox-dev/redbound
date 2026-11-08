#include "redbound/Action.h"
#include "redbound/Trigger.h"
#include "redbound/World.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace redbound {

namespace {

struct Operand {
	std::string var;
	float value = 0.f;
	static Operand parse(const std::string& s) {
		Operand o;
		char* end = nullptr;
		const float v = std::strtof(s.c_str(), &end);
		if (!s.empty() && end && *end == '\0') o.value = v; else o.var = s;
		return o;
	}
	float get(const World& w) const { return var.empty() ? value : w.getVariable(var); }
};

std::mt19937& rng() { static std::mt19937 r(12345u); return r; }

const std::string& arg(const ActionArgs& a, size_t i) {
	static const std::string empty;
	return i < a.args.size() ? a.args[i] : empty;
}

float argf(const ActionArgs& a, size_t i, float def = 0.f) {
	return i < a.args.size() ? std::strtof(a.args[i].c_str(), nullptr) : def;
}

class Nop : public Action {
public:
	bool update(ActionContext&) override { return true; }
};

class Delay : public Action {
public:
	explicit Delay(float s) : m_total(s) {}
	bool update(ActionContext& c) override {
		m_t += c.dt;
		if (m_t < m_total) return false;
		m_t = 0.f;
		return true;
	}
	void reset() override { m_t = 0.f; }
private:
	float m_total, m_t = 0.f;
};

class RandomDelay : public Action {
public:
	RandomDelay(float a, float b) : m_min(a), m_max(b) {}
	bool update(ActionContext& c) override {
		if (m_target < 0.f) m_target = std::uniform_real_distribution<float>(m_min, m_max)(rng());
		m_t += c.dt;
		if (m_t < m_target) return false;
		reset();
		return true;
	}
	void reset() override { m_t = 0.f; m_target = -1.f; }
private:
	float m_min, m_max, m_t = 0.f, m_target = -1.f;
};

class JumpToAction : public Action {
public:
	explicit JumpToAction(int target) : m_target(target) {}
	bool update(ActionContext& c) override { c.trigger.jumpTo(m_target); return false; }
private:
	int m_target;
};

class JumpToRandomAction : public Action {
public:
	explicit JumpToRandomAction(std::vector<int> t) : m_targets(std::move(t)) {}
	bool update(ActionContext& c) override {
		if (m_targets.empty()) return true;
		c.trigger.jumpTo(m_targets[std::uniform_int_distribution<size_t>(0, m_targets.size() - 1)(rng())]);
		return false;
	}
private:
	std::vector<int> m_targets;
};

class Test : public Action {
public:
	enum Op { LESS, EQUAL, GREATER, NOTEQUAL };
	Test(Op op, Operand a, Operand b, int target) : m_op(op), m_a(a), m_b(b), m_target(target) {}
	bool update(ActionContext& c) override {
		const float a = m_a.get(c.world), b = m_b.get(c.world);
		bool ok = false;
		switch (m_op) {
		case LESS: ok = a < b; break;
		case EQUAL: ok = std::fabs(a - b) < 1e-4f; break;
		case GREATER: ok = a > b; break;
		case NOTEQUAL: ok = std::fabs(a - b) >= 1e-4f; break;
		}
		if (ok) return true;
		c.trigger.jumpTo(m_target);
		return false;
	}
private:
	Op m_op;
	Operand m_a, m_b;
	int m_target;
};

class SetVariable : public Action {
public:
	SetVariable(std::string var, Operand v, bool persistent) : m_var(std::move(var)), m_v(v), m_persistent(persistent) {}
	bool update(ActionContext& c) override {
		const float v = m_v.get(c.world);
		if (m_persistent) c.world.setPersistentVariable(m_var, v); else c.world.setVariable(m_var, v);
		return true;
	}
private:
	std::string m_var;
	Operand m_v;
	bool m_persistent;
};

class ModifyVariable : public Action {
public:
	enum Op { ADD, SUB, MUL, DIV };
	ModifyVariable(Op op, std::string var, Operand v) : m_op(op), m_var(std::move(var)), m_v(v) {}
	bool update(ActionContext& c) override {
		float x = c.world.getVariable(m_var);
		const float v = m_v.get(c.world);
		switch (m_op) {
		case ADD: x += v; break;
		case SUB: x -= v; break;
		case MUL: x *= v; break;
		case DIV: if (v != 0.f) x /= v; break;
		}
		c.world.setVariable(m_var, x);
		return true;
	}
private:
	Op m_op;
	std::string m_var;
	Operand m_v;
};

class TriggerState : public Action {
public:
	enum Kind { ENABLE, DISABLE, ACTIVATE };
	TriggerState(Kind k, std::vector<std::string> t) : m_kind(k), m_triggers(std::move(t)) {}
	bool update(ActionContext& c) override {
		for (const std::string& t : m_triggers) {
			if (m_kind == ACTIVATE) c.world.activateTrigger(t);
			else c.world.setTriggerEnabled(t, m_kind == ENABLE);
		}
		return true;
	}
private:
	Kind m_kind;
	std::vector<std::string> m_triggers;
};

class ActivateRandomTrigger : public Action {
public:
	explicit ActivateRandomTrigger(std::vector<std::string> t) : m_triggers(std::move(t)) {}
	bool update(ActionContext& c) override {
		if (!m_triggers.empty())
			c.world.activateTrigger(m_triggers[std::uniform_int_distribution<size_t>(0, m_triggers.size() - 1)(rng())]);
		return true;
	}
private:
	std::vector<std::string> m_triggers;
};

class Link : public Action {
public:
	explicit Link(std::vector<std::string> a) : m_args(std::move(a)) {}
	bool update(ActionContext& c) override {
		if (m_args.empty()) return true;
		c.world.link(m_args[0], std::vector<std::string>(m_args.begin() + 1, m_args.end()));
		return true;
	}
private:
	std::vector<std::string> m_args;
};

class Unlink : public Action {
public:
	explicit Unlink(std::vector<std::string> a) : m_nodes(std::move(a)) {}
	bool update(ActionContext& c) override { c.world.unlink(m_nodes); return true; }
private:
	std::vector<std::string> m_nodes;
};

class Timer : public Action {
public:
	explicit Timer(std::string var) : m_var(std::move(var)) {}
	bool update(ActionContext& c) override {
		float v = c.world.getVariable(m_var) - c.dt;
		if (v <= 0.f) { c.world.setVariable(m_var, 0.f); return true; }
		c.world.setVariable(m_var, v);
		return false;
	}
private:
	std::string m_var;
};

class WaitForKeyPress : public Action {
public:
	explicit WaitForKeyPress(std::string key) : m_key(std::move(key)) {}
	bool update(ActionContext& c) override {
		const bool down = c.world.isKeyDown(m_key);
		const bool pressed = down && !m_wasDown;
		m_wasDown = down;
		return pressed;
	}
	void reset() override { m_wasDown = true; }
private:
	std::string m_key;
	bool m_wasDown = true;
};

class PlayAnimation : public Action {
public:
	PlayAnimation(std::string node, std::string anim, float start, float end, bool wait)
		: m_node(std::move(node)), m_anim(std::move(anim)), m_start(start), m_end(end), m_wait(wait) {}
	bool update(ActionContext& c) override {
		if (m_id < 0) {
			m_id = c.world.playAnimation(m_node, m_anim, m_start, m_end, false);
			if (m_id < 0) return true;
		}
		if (!m_wait || c.world.animationDone(m_id)) { m_id = -1; return true; }
		return false;
	}
	void reset() override { m_id = -1; }
private:
	std::string m_node, m_anim;
	float m_start, m_end;
	bool m_wait;
	int m_id = -1;
};

class Transform : public Action {
public:
	Transform(World::TransformKind k, std::string obj, std::string sub, float x, float y, float z, float dur, bool wait)
		: m_kind(k), m_obj(std::move(obj)), m_sub(std::move(sub)), m_x(x), m_y(y), m_z(z), m_dur(dur), m_wait(wait) {}
	bool update(ActionContext& c) override {
		if (m_id < 0) {
			m_id = c.world.startTransform(m_kind, m_obj, m_sub, m_x, m_y, m_z, m_dur);
			if (m_id < 0) return true;
		}
		if (!m_wait || c.world.transformDone(m_id)) { m_id = -1; return true; }
		return false;
	}
	void reset() override { m_id = -1; }
private:
	World::TransformKind m_kind;
	std::string m_obj, m_sub;
	float m_x, m_y, m_z, m_dur;
	bool m_wait;
	int m_id = -1;
};

class Fade : public Action {
public:
	Fade(float d, bool in, int color) : m_dur(d > 0.f ? d : 0.001f), m_in(in), m_color(color) {}
	bool update(ActionContext& c) override {
		m_t += c.dt;
		const float k = m_t >= m_dur ? 1.f : m_t / m_dur;
		c.world.setFade(m_in ? 1.f - k : k, m_color);
		if (m_t < m_dur) return false;
		m_t = 0.f;
		return true;
	}
	void reset() override { m_t = 0.f; }
private:
	float m_dur, m_t = 0.f;
	bool m_in;
	int m_color;
};

class Generic : public Action {
public:
	explicit Generic(std::vector<std::string> a) : m_args(std::move(a)) {}
	bool update(ActionContext& c) override { return c.world.genericAction(name(), m_args); }
private:
	std::vector<std::string> m_args;
};

int target(const ActionArgs& a, size_t i) {
	const int t = i < a.args.size() && !a.args[i].empty() ? std::atoi(a.args[i].c_str()) : a.listSize + 1;
	return t - 1;
}

}

const std::vector<std::string>& ActionRegistry::originalNames() {
	static const std::vector<std::string> names = {
		"activaterandomtrigger", "activatetrigger", "addimpulse", "addlife", "addscore", "addspeed",
		"animatetransform", "attachtorail", "break", "changeball", "checkpoint", "checkcollision", "delay",
		"disabletrigger", "enabletrigger", "fade", "finalizetrigger", "getobjectpos", "iskeypressed", "jump",
		"jumptoaction", "jumptorandomaction", "keypress", "levelcomplete", "levelend", "levelfailed", "link",
		"loadlevel", "maxspeed", "vecdist", "veclen", "vecangle", "moveobject", "moveuv", "nop", "particlefx",
		"pausetrigger", "playanimation", "animate", "playsound", "pressure", "random", "randomdelay",
		"recordsound", "resetballvalues", "rotateobject", "rotateto", "scaleobject", "setballdirection",
		"setcamera", "setcameraview", "setcamprops", "setgamemode", "setgravity", "setguidingobject",
		"setobjectpos", "setobjectscale", "setobjectstate", "setrail", "setLineSpeed", "setrenderstate",
		"settexture", "showtext", "speedmul", "stopsound", "switchobjectstate", "switchrenderstate", "timer",
		"unlink", "updateballvalues", "set", "setpersistent", "add", "sub", "mul", "div", "less", "equal",
		"greater", "notequal", "waitforidle", "waitforkey", "waitforkeyhold", "waitforkeypress",
		"waitforkeyrelease", "minigamebubbleinit", "minigamemazeinit", "minigamesbtinit"
	};
	return names;
}

ActionRegistry& ActionRegistry::instance() {
	static ActionRegistry r;
	return r;
}

ActionRegistry::ActionRegistry() {
	for (const std::string& n : originalNames())
		add(n, [](const ActionArgs& a) { return std::unique_ptr<Action>(new Generic(a.args)); });

	add("nop", [](const ActionArgs&) { return std::unique_ptr<Action>(new Nop); });
	add("delay", [](const ActionArgs& a) { return std::unique_ptr<Action>(new Delay(argf(a, 0))); });
	add("randomdelay", [](const ActionArgs& a) { return std::unique_ptr<Action>(new RandomDelay(argf(a, 0), argf(a, 1, argf(a, 0)))); });
	add("jumptoaction", [](const ActionArgs& a) { return std::unique_ptr<Action>(new JumpToAction(target(a, 0))); });
	add("jumptorandomaction", [](const ActionArgs& a) {
		std::vector<int> t;
		for (size_t i = 0; i < a.args.size(); ++i) t.push_back(std::atoi(a.args[i].c_str()) - 1);
		return std::unique_ptr<Action>(new JumpToRandomAction(t));
	});
	const char* tests[] = { "less", "equal", "greater", "notequal" };
	for (int i = 0; i < 4; ++i) {
		const Test::Op op = (Test::Op)i;
		add(tests[i], [op](const ActionArgs& a) {
			return std::unique_ptr<Action>(new Test(op, Operand::parse(arg(a, 0)), Operand::parse(arg(a, 1)), target(a, 2)));
		});
	}
	add("set", [](const ActionArgs& a) { return std::unique_ptr<Action>(new SetVariable(arg(a, 0), Operand::parse(arg(a, 1)), false)); });
	add("setpersistent", [](const ActionArgs& a) { return std::unique_ptr<Action>(new SetVariable(arg(a, 0), Operand::parse(arg(a, 1)), true)); });
	const char* mods[] = { "add", "sub", "mul", "div" };
	for (int i = 0; i < 4; ++i) {
		const ModifyVariable::Op op = (ModifyVariable::Op)i;
		add(mods[i], [op](const ActionArgs& a) {
			return std::unique_ptr<Action>(new ModifyVariable(op, arg(a, 0), Operand::parse(arg(a, 1))));
		});
	}
	add("enabletrigger", [](const ActionArgs& a) { return std::unique_ptr<Action>(new TriggerState(TriggerState::ENABLE, a.args)); });
	add("disabletrigger", [](const ActionArgs& a) { return std::unique_ptr<Action>(new TriggerState(TriggerState::DISABLE, a.args)); });
	add("activatetrigger", [](const ActionArgs& a) { return std::unique_ptr<Action>(new TriggerState(TriggerState::ACTIVATE, a.args)); });
	add("activaterandomtrigger", [](const ActionArgs& a) { return std::unique_ptr<Action>(new ActivateRandomTrigger(a.args)); });
	add("link", [](const ActionArgs& a) { return std::unique_ptr<Action>(new Link(a.args)); });
	add("unlink", [](const ActionArgs& a) { return std::unique_ptr<Action>(new Unlink(a.args)); });
	add("animate", [](const ActionArgs& a) {
		return std::unique_ptr<Action>(new PlayAnimation(arg(a, 0), arg(a, 0), 0.f, -1.f, argf(a, 1) != 0.f));
	});
	add("playanimation", [](const ActionArgs& a) {
		const float fps = 1.f / 30.f;
		const std::string anim = a.args.size() > 2 && !arg(a, 2).empty() ? arg(a, 2) : arg(a, 1);
		return std::unique_ptr<Action>(new PlayAnimation(arg(a, 0), anim, argf(a, 3) * fps,
			a.args.size() > 4 ? argf(a, 4) * fps : -1.f, false));
	});
	add("moveuv", [](const ActionArgs& a) {
		return std::unique_ptr<Action>(new Transform(World::UV, arg(a, 0), arg(a, 1), argf(a, 2), argf(a, 3), 0.f, argf(a, 4, 1.f), false));
	});
	struct T3 { const char* name; World::TransformKind kind; };
	const T3 t3[] = { { "moveobject", World::MOVE }, { "rotateobject", World::ROTATE }, { "rotateto", World::ROTATE }, { "scaleobject", World::SCALE } };
	for (const T3& t : t3) {
		const World::TransformKind k = t.kind;
		add(t.name, [k](const ActionArgs& a) {
			return std::unique_ptr<Action>(new Transform(k, arg(a, 0), arg(a, 1), argf(a, 2), argf(a, 3), argf(a, 4), argf(a, 5, 1.f), argf(a, 6) != 0.f));
		});
	}
	add("fade", [](const ActionArgs& a) {
		return std::unique_ptr<Action>(new Fade(argf(a, 0, 1.f), argf(a, 1) != 0.f, (int)argf(a, 2)));
	});
	add("waitforkeypress", [](const ActionArgs& a) { return std::unique_ptr<Action>(new WaitForKeyPress(arg(a, 0))); });
	add("timer", [](const ActionArgs& a) { return std::unique_ptr<Action>(new Timer(arg(a, 0))); });
}

std::unique_ptr<Action> ActionRegistry::create(const std::string& name, const ActionArgs& a) const {
	std::map<std::string, ActionFactory>::const_iterator it = m_factories.find(name);
	if (it == m_factories.end()) return nullptr;
	std::unique_ptr<Action> act = it->second(a);
	act->setName(name);
	return act;
}

}
