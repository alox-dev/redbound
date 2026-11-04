#include "Paths.h"
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace redbound {

bool makeDirectory(const std::string& path) {
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0;
#else
	return mkdir(path.c_str(), 0755) == 0;
#endif
}

void makeDirectories(const std::string& path) {
	std::string current;
	for (char c : path) {
		current += c;
		if ((c == '/' || c == '\\') && current.size() > 1) makeDirectory(current);
	}
	makeDirectory(path);
}

static std::string fromEnv(const char* name) {
	const char* v = getenv(name);
	return v && *v ? std::string(v) : std::string();
}

std::string userDataDir() {
#ifdef _WIN32
	const std::string appData = fromEnv("APPDATA");
	return (appData.empty() ? std::string(".") : appData) + "/redbound";
#else
	const std::string xdg = fromEnv("XDG_DATA_HOME");
	const std::string home = fromEnv("HOME");
	return (xdg.empty() ? (home.empty() ? std::string(".") : home) + "/.local/share" : xdg) + "/redbound";
#endif
}

std::string userConfigDir() {
#ifdef _WIN32
	return userDataDir();
#else
	const std::string xdg = fromEnv("XDG_CONFIG_HOME");
	const std::string home = fromEnv("HOME");
	return (xdg.empty() ? (home.empty() ? std::string(".") : home) + "/.config" : xdg) + "/redbound";
#endif
}

void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
	_putenv_s(name, value ? value : "");
#else
	if (value) setenv(name, value, 1);
	else unsetenv(name);
#endif
}

}
