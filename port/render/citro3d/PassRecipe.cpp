#include "PassRecipe.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace redbound {

static std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)toupper(c); });
	return s;
}

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s;
}

static bool isIdent(char c) { return isalnum((unsigned char)c) || c == '_'; }

std::vector<std::string> pixelSamplers(const std::string& source) {
	std::vector<std::string> out;
	size_t at = 0;
	while ((at = source.find("sampler2D", at)) != std::string::npos) {
		const size_t lineStart = source.rfind('\n', at);
		const std::string head = source.substr(lineStart == std::string::npos ? 0 : lineStart + 1, at - (lineStart == std::string::npos ? 0 : lineStart + 1));
		at += 9;
		if (head.find("uniform") == std::string::npos) continue;
		while (at < source.size() && isspace((unsigned char)source[at])) ++at;
		size_t end = at;
		while (end < source.size() && isIdent(source[end])) ++end;
		if (end == at) continue;
		const std::string name = source.substr(at, end - at);
		if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
		at = end;
	}
	return out;
}

std::vector<std::string> sampledTextures(const std::string& source) {
	const std::vector<std::string> declared = pixelSamplers(source);
	std::vector<std::string> out;
	static const char* const CALLS[] = { "texture2D", "texture2DProj", "texture2DLod", "textureCube" };
	for (const char* call : CALLS) {
		const size_t len = strlen(call);
		size_t at = 0;
		while ((at = source.find(call, at)) != std::string::npos) {
			const bool boundary = at == 0 || !isIdent(source[at - 1]);
			size_t p = at + len;
			at = p;
			if (!boundary) continue;
			while (p < source.size() && isspace((unsigned char)source[p])) ++p;
			if (p >= source.size() || source[p] != '(') continue;
			++p;
			while (p < source.size() && isspace((unsigned char)source[p])) ++p;
			size_t end = p;
			while (end < source.size() && isIdent(source[end])) ++end;
			if (end == p) continue;
			const std::string name = source.substr(p, end - p);
			if (std::find(declared.begin(), declared.end(), name) == declared.end()) continue;
			if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
		}
	}
	std::vector<std::string> ordered;
	for (const std::string& n : declared)
		if (std::find(out.begin(), out.end(), n) != out.end()) ordered.push_back(n);
	return ordered;
}

static bool hasWord(const std::string& source, const std::string& word) {
	size_t at = 0;
	while ((at = source.find(word, at)) != std::string::npos) {
		const bool left = at == 0 || !isIdent(source[at - 1]);
		const bool right = at + word.size() >= source.size() || !isIdent(source[at + word.size()]);
		if (left && right) return true;
		at += word.size();
	}
	return false;
}

static bool isAuxiliarySampler(const std::string& name) {
	return name == "SIMAP" || name == "NORMALMAP" || name == "REFLMAP" || name == "REFRACMAP";
}

static CompareFunc compareFunc(const std::string& v, CompareFunc def) {
	if (v == "NEVER") return CompareFunc::Never;
	if (v == "LESS") return CompareFunc::Less;
	if (v == "EQUAL") return CompareFunc::Equal;
	if (v == "LEQUAL") return CompareFunc::LessEqual;
	if (v == "GREATER") return CompareFunc::Greater;
	if (v == "NOTEQUAL") return CompareFunc::NotEqual;
	if (v == "GEQUAL") return CompareFunc::GreaterEqual;
	if (v == "ALWAYS") return CompareFunc::Always;
	return def;
}

static BlendFactor blendFactor(const std::string& v, BlendFactor def) {
	if (v == "ZERO") return BlendFactor::Zero;
	if (v == "ONE") return BlendFactor::One;
	if (v == "SRC_COLOR") return BlendFactor::SrcColor;
	if (v == "ONE_MINUS_SRC_COLOR") return BlendFactor::OneMinusSrcColor;
	if (v == "DST_COLOR") return BlendFactor::DstColor;
	if (v == "ONE_MINUS_DST_COLOR") return BlendFactor::OneMinusDstColor;
	if (v == "SRC_ALPHA") return BlendFactor::SrcAlpha;
	if (v == "ONE_MINUS_SRC_ALPHA") return BlendFactor::OneMinusSrcAlpha;
	if (v == "DST_ALPHA") return BlendFactor::DstAlpha;
	if (v == "ONE_MINUS_DST_ALPHA") return BlendFactor::OneMinusDstAlpha;
	if (v == "SRC_ALPHA_SATURATE") return BlendFactor::SrcAlphaSaturate;
	return def;
}

PassRecipe makePassRecipe(const std::string& pixelSource, const std::map<std::string, std::string>& states, bool water) {
	PassRecipe r;
	r.samplers = sampledTextures(pixelSource);
	static const char* const BASE_NAMES[] = { "BASEMAP", "DIFFUSEMAP" };
	for (const char* n : BASE_NAMES)
		if (r.baseSampler.empty() && std::find(r.samplers.begin(), r.samplers.end(), n) != r.samplers.end()) r.baseSampler = n;
	for (const std::string& s : r.samplers)
		if (r.baseSampler.empty() && !isAuxiliarySampler(s)) r.baseSampler = s;
	if (std::find(r.samplers.begin(), r.samplers.end(), "SIMAP") != r.samplers.end()) r.lightmapSampler = "SIMAP";
	r.alphaTest = hasWord(pixelSource, "discard");

	if (water) r.combiner = PassCombiner::Water;
	else if (!r.baseSampler.empty() && !r.lightmapSampler.empty()) r.combiner = PassCombiner::Lightmap;
	else if (!r.baseSampler.empty()) r.combiner = PassCombiner::Texture;
	else r.combiner = PassCombiner::VertexColor;

	std::map<std::string, std::string> st;
	for (const auto& kv : states) st[lower(kv.first)] = upper(kv.second);
	auto get = [&](const char* key) { auto it = st.find(key); return it == st.end() ? std::string() : it->second; };

	if (get("enableculling") == "FALSE") r.cull = CullMode::None;
	else {
		const std::string face = get("cullface");
		r.cull = face == "FRONT" ? CullMode::Front : face == "FRONT_AND_BACK" ? CullMode::None : CullMode::Back;
	}
	r.overlay = get("enabledepthtest") == "FALSE";
	r.depthWrite = !r.overlay && get("enabledepthmask") != "FALSE";
	r.depthFunc = compareFunc(get("depthfunc"), CompareFunc::Less);
	r.blend = get("enableblending") == "TRUE";
	if (r.blend) {
		const std::string eq = get("blendequation");
		r.blendEquation = eq == "FUNC_SUBTRACT" ? BlendEquation::Subtract : eq == "FUNC_REVERSE_SUBTRACT" ? BlendEquation::ReverseSubtract : BlendEquation::Add;
		r.blendSrc = blendFactor(get("blendfuncsrc"), BlendFactor::One);
		r.blendDst = blendFactor(get("blendfuncdst"), BlendFactor::Zero);
	}
	if (water && !r.blend) {
		r.blend = true;
		r.blendSrc = BlendFactor::SrcAlpha;
		r.blendDst = BlendFactor::OneMinusSrcAlpha;
	}
	return r;
}

DepthBias materialDepthBias(const std::string& materialName, const PassRecipe& recipe) {
	if (recipe.overlay) return DepthBias::Overlay;
	if (materialName == "rail_boost") return DepthBias::DecalNear;
	if (materialName == "boost_glow") return DepthBias::DecalFar;
	return DepthBias::None;
}

bool materialIsSky(const std::string& materialName) {
	return lower(materialName).find("skybox") != std::string::npos;
}

const char* combinerName(PassCombiner c) {
	switch (c) {
	case PassCombiner::Texture: return "texture";
	case PassCombiner::Lightmap: return "lightmap";
	case PassCombiner::Water: return "water";
	default: return "vertex color";
	}
}

}
