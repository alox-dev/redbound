#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace redbound {

class Strings {
public:
	static Strings& get();
	void load(const std::string& language);
	const char* tr(const char* key) const;
	const std::string& current() const { return m_code; }
	std::vector<std::pair<std::string, std::string>> available() const;
private:
	std::map<std::string, std::string> m_strings;
	std::map<std::string, std::string> m_fallback;
	std::string m_code;
	static std::string systemLanguage();
	static bool readFile(const std::string& code, std::map<std::string, std::string>& out);
	static std::vector<std::string> codes();
};

inline const char* tr(const char* key) { return Strings::get().tr(key); }

}
