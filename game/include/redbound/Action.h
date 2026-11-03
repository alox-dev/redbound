#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace redbound {

class World;
class Trigger;

struct ActionContext {
	World& world;
	Trigger& trigger;
	float dt;
};

class Action {
public:
	virtual ~Action() = default;
	virtual bool update(ActionContext& ctx) = 0;
	virtual void reset() {}
	const std::string& name() const { return m_name; }
	void setName(const std::string& n) { m_name = n; }

private:
	std::string m_name;
};

struct ActionArgs {
	std::vector<std::string> args;
	int listSize = 0;
};

using ActionFactory = std::function<std::unique_ptr<Action>(const ActionArgs&)>;

class ActionRegistry {
public:
	static ActionRegistry& instance();
	void add(const std::string& name, ActionFactory f) { m_factories[name] = std::move(f); }
	bool has(const std::string& name) const { return m_factories.count(name) != 0; }
	std::unique_ptr<Action> create(const std::string& name, const ActionArgs& a) const;
	static const std::vector<std::string>& originalNames();

private:
	ActionRegistry();
	std::map<std::string, ActionFactory> m_factories;
};

}
