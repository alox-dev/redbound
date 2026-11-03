#include "I18n.h"
#include <SDL.h>
#include <cstdlib>

namespace redbound {

Strings& Strings::get() {
	static Strings s;
	return s;
}

static std::string readAll(const std::string& path) {
	SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
	if (!rw) return "";
	std::string text;
	char buffer[4096];
	size_t n;
	while ((n = SDL_RWread(rw, buffer, 1, sizeof(buffer))) > 0) text.append(buffer, n);
	SDL_RWclose(rw);
	return text;
}

static std::vector<std::string> searchDirs() {
	std::vector<std::string> dirs;
	if (const char* home = getenv("HOME")) dirs.push_back(std::string(home) + "/.config/redbound/lang/");
#ifdef __ANDROID__
	dirs.push_back("lang/");
#elif defined(REDBOUND_LANG_DIR)
	dirs.push_back(std::string(REDBOUND_LANG_DIR) + "/");
#endif
	return dirs;
}

static std::string trim(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos) return "";
	return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

static std::string unescape(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
			out += '\n';
			++i;
		} else {
			out += s[i];
		}
	}
	return out;
}

bool Strings::readFile(const std::string& code, std::map<std::string, std::string>& out) {
	bool found = false;
	const std::vector<std::string> dirs = searchDirs();
	for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
		const std::string text = readAll(*it + code + ".lang");
		if (text.empty()) continue;
		found = true;
		size_t a = 0;
		while (a < text.size()) {
			const size_t e = text.find('\n', a);
			const std::string line = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
			a = e == std::string::npos ? text.size() : e + 1;
			const std::string t = trim(line);
			if (t.empty() || t[0] == '#') continue;
			const size_t eq = t.find('=');
			if (eq == std::string::npos) continue;
			out[trim(t.substr(0, eq))] = unescape(trim(t.substr(eq + 1)));
		}
	}
	return found;
}

std::vector<std::string> Strings::codes() {
	std::vector<std::string> list;
	for (const std::string& dir : searchDirs()) {
		const std::string text = readAll(dir + "languages");
		size_t a = 0;
		while (a < text.size()) {
			const size_t e = text.find('\n', a);
			const std::string code = trim(text.substr(a, e == std::string::npos ? std::string::npos : e - a));
			a = e == std::string::npos ? text.size() : e + 1;
			bool known = false;
			for (const std::string& c : list) known = known || c == code;
			if (!code.empty() && code[0] != '#' && !known) list.push_back(code);
		}
	}
	if (list.empty()) list.push_back("en");
	return list;
}

std::string Strings::systemLanguage() {
	std::string code = "en";
	if (SDL_Locale* locales = SDL_GetPreferredLocales()) {
		if (locales[0].language) code = locales[0].language;
		SDL_free(locales);
	}
	return code;
}

void Strings::load(const std::string& language) {
	m_fallback.clear();
	readFile("en", m_fallback);
	std::string code = language.empty() || language == "auto" ? systemLanguage() : language;
	m_strings.clear();
	if (!readFile(code, m_strings)) code = "en";
	m_code = code;
}

const char* Strings::tr(const char* key) const {
	auto it = m_strings.find(key);
	if (it != m_strings.end()) return it->second.c_str();
	it = m_fallback.find(key);
	return it != m_fallback.end() ? it->second.c_str() : key;
}

std::vector<std::pair<std::string, std::string>> Strings::available() const {
	std::vector<std::pair<std::string, std::string>> list;
	for (const std::string& code : codes()) {
		std::map<std::string, std::string> strings;
		if (!readFile(code, strings)) continue;
		const auto name = strings.find("language.name");
		list.push_back(std::make_pair(code, name != strings.end() ? name->second : code));
	}
	return list;
}

}
