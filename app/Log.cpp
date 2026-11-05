#include "Log.h"
#include "Paths.h"
#include "AndroidBridge.h"
#include <SDL.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace redbound {

static std::string logDirectory() {
#ifdef __ANDROID__
	const std::string external = androidExternalDir();
	if (!external.empty()) return external;
#endif
	return userDataDir();
}

std::string startLog() {
	const std::string dir = logDirectory();
	makeDirectories(dir);
	const std::string path = dir + "/redbound.log";
	const std::string previous = dir + "/redbound.previous.log";
	remove(previous.c_str());
	rename(path.c_str(), previous.c_str());
#ifdef _WIN32
	if (!freopen(path.c_str(), "w", stdout)) return std::string();
	setvbuf(stdout, 0, _IOLBF, 0);
	_dup2(_fileno(stdout), 2);
	setvbuf(stderr, 0, _IONBF, 0);
#else
	FILE* file = fopen(path.c_str(), "w");
	if (!file) return std::string();
	static int pipeFds[2];
	if (pipe(pipeFds) != 0) {
		fclose(file);
		return std::string();
	}
	const int console = dup(1);
	fflush(stdout);
	fflush(stderr);
	dup2(pipeFds[1], 1);
	dup2(pipeFds[1], 2);
	setvbuf(stdout, 0, _IOLBF, 0);
	setvbuf(stderr, 0, _IOLBF, 0);
	std::thread([file, console] {
		char buffer[1024];
		std::string line;
		ssize_t n;
		while ((n = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
			for (ssize_t i = 0; i < n; ++i) {
				line += buffer[i];
				if (buffer[i] != '\n') continue;
				fputs(line.c_str(), file);
				fflush(file);
#ifdef __ANDROID__
				line.pop_back();
				__android_log_write(ANDROID_LOG_INFO, "Redbound", line.c_str());
				(void)console;
#else
				if (console >= 0 && write(console, line.data(), line.size()) < 0) {}
#endif
				line.clear();
			}
		}
	}).detach();
#endif
	const time_t now = time(0);
	char stamp[64];
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
	SDL_version sdl;
	SDL_GetVersion(&sdl);
	printf("Redbound %s on %s, SDL %d.%d.%d, started %s\n", REDBOUND_VERSION, SDL_GetPlatform(), sdl.major, sdl.minor, sdl.patch, stamp);
	printf("log: %s\n", path.c_str());
	return path;
}

}
