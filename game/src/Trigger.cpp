#include "redbound/Trigger.h"
#include "redbound/World.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <set>

namespace redbound {

static std::string trim(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

Trigger::Trigger(const std::string& name) : m_name(name), m_state(ENABLED) {}

void Trigger::parseList(char prefix, std::vector<std::unique_ptr<Action>>& out, std::vector<std::string>& warnings) {
	for (int i = 1; i < 1000; ++i) {
		char key[8];
		snprintf(key, sizeof(key), i < 100 ? "%c%02d" : "%c%03d", prefix, i);
		std::map<std::string, std::string>::const_iterator it = m_props.find(key);
		if (it == m_props.end()) break;
		std::vector<std::string> parts;
		std::stringstream ss(it->second);
		std::string tok;
		while (std::getline(ss, tok, ',')) {
			tok = trim(tok);
			if (!tok.empty()) parts.push_back(tok);
		}
		if (parts.empty()) { warnings.push_back(m_name + "." + key + ": empty"); continue; }
		ActionArgs a;
		a.args.assign(parts.begin() + 1, parts.end());
		a.listSize = (int)out.size();
		std::unique_ptr<Action> act = ActionRegistry::instance().create(parts[0], a);
		if (!act) {
			warnings.push_back(m_name + "." + key + ": unknown action '" + parts[0] + "' (using nop)");
			act = ActionRegistry::instance().create("nop", a);
		}
		out.push_back(std::move(act));
	}
}

std::vector<std::string> Trigger::parse(const std::string& properties) {
	std::vector<std::string> warnings;
	m_props.clear();
	std::stringstream ss(properties);
	std::string line;
	while (std::getline(ss, line)) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string k = trim(line.substr(0, eq));
		if (!k.empty()) m_props[k] = trim(line.substr(eq + 1));
	}
	if (m_props.count("state")) m_state = (State)std::atoi(m_props["state"].c_str());
	if (m_props.count("executetype")) m_executeType = std::atoi(m_props["executetype"].c_str());
	if (m_props.count("finalizeonkeypress")) {
		const std::string v = m_props["finalizeonkeypress"];
		m_finalizeOnKeyPress = v == "1" || v == "true" || v == "TRUE";
	}
	m_actions.clear();
	m_exitActions.clear();
	parseList('a', m_actions, warnings);
	parseList('e', m_exitActions, warnings);
	if (m_state == RUNNING) setState(RUNNING);
	return warnings;
}

void Trigger::setState(State s) {
	if (s == RUNNING) {
		m_disableAfterRun = m_enableAfterRun = false;
		m_index = 0;
		for (size_t i = 0; i < m_actions.size(); ++i) m_actions[i]->reset();
	} else if (s == EXITING) {
		m_exitIndex = 0;
		for (size_t i = 0; i < m_exitActions.size(); ++i) m_exitActions[i]->reset();
	}
	m_state = s;
}

void Trigger::finishThenSet(State s) {
	if (m_state != RUNNING && m_state != EXITING) { setState(s); return; }
	m_disableAfterRun = (s == DISABLED);
	m_enableAfterRun = (s == ENABLED);
}

void Trigger::jumpTo(int index) {
	if (m_state == EXITING) m_exitIndex = index; else m_index = index;
}

void Trigger::update(World& world, float dt) {
	ActionContext ctx = { world, *this, dt };

	if (m_state != DISABLED) {
		for (size_t i = 0; i < m_parallel.size();) {
			if (m_parallel[i]->update(ctx)) m_parallel.erase(m_parallel.begin() + i);
			else ++i;
		}
	}

	if (m_state == RUNNING) {
		while (m_index >= 0 && m_index < (int)m_actions.size()) {
			static const bool trace = getenv("REDBOUND_TRACE") != 0;
			if (trace) {
				static std::set<std::string> seen;
				char key[16];
				snprintf(key, sizeof(key), "#a%02d", m_index + 1);
				if (seen.insert(m_name + key).second) printf("[trace] %s a%02d %s\n", m_name.c_str(), m_index + 1, m_actions[m_index]->name().c_str());
			}
			const int before = m_index;
			if (!m_actions[m_index]->update(ctx)) return;
			if (m_state != RUNNING) return;
			if (m_index == before) ++m_index;
		}
		if (m_index < 0 || m_index > (int)m_actions.size())
			fprintf(stderr, "[trigger %s] invalid action index: %d\n", m_name.c_str(), m_index);
		setState(EXITING);
	}
	if (m_state == EXITING) {
		while (m_exitIndex >= 0 && m_exitIndex < (int)m_exitActions.size()) {
			const int before = m_exitIndex;
			if (!m_exitActions[m_exitIndex]->update(ctx)) return;
			if (m_state != EXITING) return;
			if (m_exitIndex == before) ++m_exitIndex;
		}
		const bool disable = m_disableAfterRun || (m_executeType == 0 && !m_enableAfterRun);
		m_disableAfterRun = m_enableAfterRun = false;
		m_state = disable ? DISABLED : ENABLED;
	}
}

}
