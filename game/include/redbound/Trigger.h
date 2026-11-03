#pragma once
#include "Action.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace redbound {

class World;

class Trigger {
public:
	enum State { DISABLED = 0, ENABLED = 1, RUNNING = 2, EXITING = 3 };

	explicit Trigger(const std::string& name = std::string());

	std::vector<std::string> parse(const std::string& properties);

	void setState(State s);
	void finishThenSet(State s);
	State state() const { return m_state; }
	void activate() { if (m_state == ENABLED) setState(RUNNING); }
	void update(World& world, float dt);

	void jumpTo(int index);
	bool executingExitList() const { return m_state == EXITING; }

	const std::string& name() const { return m_name; }
	const std::map<std::string, std::string>& properties() const { return m_props; }
	size_t actionCount() const { return m_actions.size(); }
	size_t exitActionCount() const { return m_exitActions.size(); }
	int currentIndex() const { return m_index; }

	int executeType() const { return m_executeType; }

private:
	bool m_disableAfterRun = false, m_enableAfterRun = false;
	std::string m_name;
	std::map<std::string, std::string> m_props;
	std::vector<std::unique_ptr<Action>> m_actions, m_exitActions, m_parallel;
	State m_state = DISABLED;
	int m_index = 0, m_exitIndex = 0;
	int m_executeType = 1;
	bool m_finalizeOnKeyPress = false;

	void parseList(char prefix, std::vector<std::unique_ptr<Action>>& out, std::vector<std::string>& warnings);
};

}
