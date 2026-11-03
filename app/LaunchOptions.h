#pragma once
#include <string>
#include <utility>
#include <vector>

namespace redbound {

struct LaunchOptions {
	std::string dataDir;
	std::string levelFile;
	std::string cameraName;
	std::string screenshot;
	std::string importDeb;
	int frames = 0;
	int width = 800;
	int height = 480;
	bool play = false;
	bool menu = false;
	bool fromMenu = false;
	bool dev = false;
	std::vector<std::pair<std::string, float>> variables;

	void parse(const std::vector<std::string>& args);
	void clearLevel();
	void resolveDataDir();
	std::string shaderDir() const;
	std::string textureDir() const;
	std::string particleDir() const;
	std::string levelDir() const;
};

}
