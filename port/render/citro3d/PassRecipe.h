#pragma once
#include <map>
#include <string>
#include <vector>

namespace redbound {

enum class PassCombiner { VertexColor, Texture, Lightmap, Water };
enum class CullMode { None, Back, Front };
enum class CompareFunc { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendFactor { Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha, SrcAlphaSaturate };
enum class BlendEquation { Add, Subtract, ReverseSubtract };
enum class DepthBias { None, Overlay, DecalNear, DecalFar };

struct PassRecipe {
	PassCombiner combiner = PassCombiner::VertexColor;
	std::string baseSampler;
	std::string lightmapSampler;
	std::vector<std::string> samplers;
	bool alphaTest = false;
	CullMode cull = CullMode::Back;
	bool depthWrite = true;
	bool overlay = false;
	CompareFunc depthFunc = CompareFunc::Less;
	bool blend = false;
	BlendEquation blendEquation = BlendEquation::Add;
	BlendFactor blendSrc = BlendFactor::One;
	BlendFactor blendDst = BlendFactor::Zero;
};

std::vector<std::string> pixelSamplers(const std::string& source);
std::vector<std::string> sampledTextures(const std::string& source);
PassRecipe makePassRecipe(const std::string& pixelSource, const std::map<std::string, std::string>& states, bool water);
DepthBias materialDepthBias(const std::string& materialName, const PassRecipe& recipe);
bool materialIsSky(const std::string& materialName);
const char* combinerName(PassCombiner c);

}
