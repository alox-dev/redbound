#include "LaunchOptions.h"
#include "FileUtils.h"
#include <cstdio>
#include <cstdlib>

namespace redbound {

void LaunchOptions::parse(const std::vector<std::string>& args) {
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string& a = args[i];
		const bool hasValue = i + 1 < args.size();
		if (a == "--data" && hasValue) dataDir = args[++i];
		else if (a == "--camera" && hasValue) cameraName = args[++i];
		else if (a == "--screenshot" && hasValue) screenshot = args[++i];
		else if (a == "--frames" && hasValue) frames = atoi(args[++i].c_str());
		else if (a == "--size" && hasValue) sscanf(args[++i].c_str(), "%dx%d", &width, &height);
		else if (a == "--import-deb" && hasValue) importDeb = args[++i];
		else if (a == "--var" && hasValue) {
			const std::string kv = args[++i];
			const size_t eq = kv.find('=');
			if (eq != std::string::npos) variables.push_back(std::make_pair(kv.substr(0, eq), (float)atof(kv.c_str() + eq + 1)));
		}
		else if (a == "--play") play = true;
		else if (a == "--menu") menu = true;
		else if (a == "--from-menu") fromMenu = true;
		else if (a == "--dev") dev = true;
		else levelFile = a;
	}
}

void LaunchOptions::clearLevel() {
	play = menu = fromMenu = false;
	levelFile.clear();
	cameraName.clear();
	screenshot.clear();
	frames = 0;
	variables.clear();
}

void LaunchOptions::resolveDataDir() {
	if (!dataDir.empty() || levelFile.empty()) return;
	std::string up = parentDirectory(parentDirectory(parentDirectory(levelFile)));
	if (parentDirectory(levelFile) == ".") up = "../..";
	if (isDirectory(up + "/resources")) dataDir = up;
}

std::string LaunchOptions::shaderDir() const { return dataDir.empty() ? "" : withTrailingSlash(dataDir + "/shaders"); }
std::string LaunchOptions::textureDir() const { return dataDir.empty() ? "" : withTrailingSlash(dataDir + "/resources/textures"); }
std::string LaunchOptions::particleDir() const { return dataDir.empty() ? "" : withTrailingSlash(dataDir + "/resources/particles"); }

std::string LaunchOptions::levelDir() const {
	const size_t slash = levelFile.find_last_of('/');
	return slash == std::string::npos ? std::string() : levelFile.substr(0, slash + 1);
}

}
