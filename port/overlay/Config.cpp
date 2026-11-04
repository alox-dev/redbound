#include "Config.h"
#include "Paths.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace redbound {

Config& Config::get() { static Config c; return c; }

std::string Config::path() const { return userConfigDir() + "/config.ini"; }

void Config::load() {
	FILE* f = fopen(path().c_str(), "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		std::string s = line;
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
		const size_t eq = s.find('=');
		if (eq == std::string::npos || s[0] == '#') continue;
		m_v[s.substr(0, eq)] = s.substr(eq + 1);
	}
	fclose(f);
}

void Config::save() const {
	const std::string p = path();
	makeDirectories(p.substr(0, p.find_last_of('/')));
	FILE* f = fopen(p.c_str(), "w");
	if (!f) return;
	fprintf(f, "# Redbound settings\n");
	for (const auto& kv : m_v) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
	fclose(f);
}

std::string Config::str(const std::string& k, const std::string& def) const { auto it = m_v.find(k); return it == m_v.end() ? def : it->second; }
bool Config::flag(const std::string& k, bool def) const { auto it = m_v.find(k); return it == m_v.end() ? def : it->second == "1"; }
float Config::num(const std::string& k, float def) const { auto it = m_v.find(k); return it == m_v.end() ? def : (float)atof(it->second.c_str()); }
void Config::setNum(const std::string& k, float v) { char b[32]; snprintf(b, sizeof(b), "%g", v); m_v[k] = b; }

void Config::applyToEnvironment() const {
	const char* map[][2] = {
		{ "improvements.visible_sections", "REDBOUND_ORIGINAL_SECTIONS" },
	};
	for (auto& m : map) {
		setEnvironment(m[1], flag(m[0], true) ? nullptr : "1");
	}
}

}
