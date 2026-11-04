#include "AppContext.h"
#include "Config.h"
#include "DebImport.h"
#include "Devices.h"
#include "LaunchOptions.h"
#include "LevelSession.h"
#include "Overlay.h"
#include <GLES2/gl2.h>
#include <SDL.h>
#include <lang/Format.h>
#include <lang/Throwable.h>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>
#ifdef __ANDROID__
#include <android/log.h>
#include <cstdlib>
#include <thread>
#endif

using namespace redbound;

#ifdef __ANDROID__
static void redirectOutputToLogcat() {
	static int pipeFds[2];
	if (pipe(pipeFds) != 0) return;
	setvbuf(stderr, 0, _IOLBF, 0);
	dup2(pipeFds[1], 1);
	dup2(pipeFds[1], 2);
	std::thread([] {
		char buffer[1024];
		std::string line;
		ssize_t n;
		while ((n = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
			for (ssize_t i = 0; i < n; ++i) {
				if (buffer[i] == '\n') {
					__android_log_write(ANDROID_LOG_INFO, "Redbound", line.c_str());
					line.clear();
				} else {
					line += buffer[i];
				}
			}
		}
	}).detach();
}

static void useAppStorage() {
	if (const char* internal = SDL_AndroidGetInternalStoragePath()) {
		setenv("HOME", internal, 1);
		setenv("XDG_DATA_HOME", internal, 1);
		setenv("XDG_CONFIG_HOME", internal, 1);
	}
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
}
#endif

static int importFromCommandLine(const std::string& deb) {
	const std::string dest = defaultGameDir();
	printf("importing %s into %s...\n", deb.c_str(), dest.c_str());
	const DebImportResult r = importDeb(deb, dest, [](const std::string&, int n) {
		if (n % 50 == 0) printf("  %d files\n", n);
	});
	if (!r.ok) {
		fprintf(stderr, "import failed: %s\n", r.error.c_str());
		return 1;
	}
	printf("done: %d files (%.1f MB). Run the game without arguments to play.\n", r.files, r.bytes / 1048576.0);
	return 0;
}

static bool useImportedData(LaunchOptions& options) {
	const std::string game = defaultGameDir();
	if (!gameDataPresent(game) || chdir((game + "/data/bundles/original").c_str()) != 0) return false;
	options.levelFile = "level01.hgr";
	options.menu = true;
	options.dataDir.clear();
	options.resolveDataDir();
	return true;
}

static bool createWindow(AppContext& app, const LaunchOptions& options, SDL_GLContext& gl) {
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#ifdef _WIN32
	SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
	SDL_SetHint(SDL_HINT_VIDEO_WIN_D3DCOMPILER, "d3dcompiler_47.dll");
#endif
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0 && SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __ANDROID__
	(void)options;
	app.window = SDL_CreateWindow("Redbound", 0, 0, 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
#else
	app.window = SDL_CreateWindow("Redbound", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, options.width, options.height,
	                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (options.screenshot.empty() ? 0 : SDL_WINDOW_HIDDEN));
#endif
	if (!app.window) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return false;
	}
	gl = SDL_GL_CreateContext(app.window);
	if (!gl) {
		fprintf(stderr, "SDL_GL_CreateContext (GLES 2.0): %s\n", SDL_GetError());
		return false;
	}
	SDL_GL_GetDrawableSize(app.window, &app.width, &app.height);
	SDL_GL_SetSwapInterval(Config::get().flag("graphics.vsync", true) ? 1 : 0);
	if (Config::get().flag("graphics.fullscreen", false)) SDL_SetWindowFullscreen(app.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	printf("GL: %s | %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
	return true;
}

static bool runImportScreen(AppContext& app) {
	Overlay& overlay = *app.overlay;
	overlay.setImportMode(true);
	bool quit = false;
	overlay.onQuit = [&]() { quit = true; };
	while (!quit && !gameDataPresent(defaultGameDir())) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) quit = true;
			overlay.processEvent(e);
		}
		SDL_GL_GetDrawableSize(app.window, &app.width, &app.height);
		glViewport(0, 0, app.width, app.height);
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		overlay.draw();
		SDL_GL_SwapWindow(app.window);
		SDL_Delay(16);
	}
	overlay.setImportMode(false);
	overlay.setOpen(false);
	return !quit;
}

int main(int argc, char** argv) {
	setvbuf(stdout, 0, _IOLBF, 0);
#ifdef __ANDROID__
	redirectOutputToLogcat();
	useAppStorage();
#endif
	Config::get().load();
	Config::get().applyToEnvironment();
	LaunchOptions options;
	options.parse(std::vector<std::string>(argv + 1, argv + argc));
	if (!options.importDeb.empty()) return importFromCommandLine(options.importDeb);
	bool needImport = false;
	if (options.levelFile.empty() && !useImportedData(options)) {
		printf("game data not found in %s: opening the import screen (or run with --import-deb file.deb)\n", defaultGameDir().c_str());
		needImport = true;
	}
	options.resolveDataDir();
	AppContext app;
	SDL_GLContext gl = 0;
	if (!createWindow(app, options, gl)) return 1;
	Devices::instance().init();
	Overlay overlay;
	overlay.init(app.window, gl);
	overlay.onCalibrate = []() { Devices::instance().requestRecalibration(); };
	app.overlay = &overlay;
	int exitCode = 0;
	if (needImport && (!runImportScreen(app) || !useImportedData(options))) {
		overlay.shutdown();
		SDL_GL_DeleteContext(gl);
		SDL_DestroyWindow(app.window);
		SDL_Quit();
		return 0;
	}
	for (;;) {
		SessionResult result;
		try {
			LevelSession session(app, options);
			result = session.run();
		} catch (lang::Throwable& e) {
			fprintf(stderr, "ERROR: %s\n", e.getMessage().format().c_str());
			exitCode = 2;
			break;
		}
		if (result.quit || result.next.empty()) break;
		options.clearLevel();
		options.parse(result.next);
		printf("--- level change: %s%s%s ---\n", options.levelFile.c_str(), options.play ? " (play)" : "", options.menu ? " (menu)" : "");
	}
	overlay.shutdown();
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(app.window);
	SDL_Quit();
	return exitCode;
}
