#pragma once
#include <map>
#include <string>
#include <vector>

namespace redbound {

struct EffectPass {
	std::string name;
	std::string vertexShaderPath;
	std::string pixelShaderPath;
	std::map<std::string, std::string> states;
};

struct EffectTechnique {
	std::string name;
	std::vector<EffectPass> passes;
};

struct Effect {
	std::string path;
	std::vector<std::string> defines;
	std::map<std::string, std::string> globals;
	std::vector<EffectTechnique> techniques;
	std::string error;
};

bool parseEffect(const std::string& path, const std::vector<std::string>& extraDefines, Effect& out);

bool buildShaderSource(const std::string& path, const std::vector<std::string>& defines, bool fragment,
                       std::string& source, std::string& error);

std::string dirOf(const std::string& path);
std::string joinPath(const std::string& dir, const std::string& rel);

}
