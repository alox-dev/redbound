#include "Citro3D.h"
#include "ImageLoader.h"
#include "TextureTiling.h"
#include <math/float4.h>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "scene_shbin.h"

USING_NAMESPACE(lang)
USING_NAMESPACE(math)
USING_NAMESPACE(redbound)

BEGIN_NAMESPACE(gr)

bool citro3dShuttingDown = false;

static bool fileExists(const std::string& p) {
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string baseName(const std::string& p) {
	const size_t i = p.find_last_of("/\\");
	return i == std::string::npos ? p : p.substr(i + 1);
}

static GPU_TESTFUNC depthFunc(redbound::CompareFunc f, bool inverted) {
	switch (f) {
	case CompareFunc::Never: return GPU_NEVER;
	case CompareFunc::Always: return GPU_ALWAYS;
	case CompareFunc::Equal: return GPU_EQUAL;
	case CompareFunc::NotEqual: return GPU_NOTEQUAL;
	case CompareFunc::Less: return inverted ? GPU_GREATER : GPU_LESS;
	case CompareFunc::LessEqual: return inverted ? GPU_GEQUAL : GPU_LEQUAL;
	case CompareFunc::Greater: return inverted ? GPU_LESS : GPU_GREATER;
	default: return inverted ? GPU_LEQUAL : GPU_GEQUAL;
	}
}

static GPU_BLENDFACTOR blendFactor(redbound::BlendFactor f) {
	switch (f) {
	case BlendFactor::Zero: return GPU_ZERO;
	case BlendFactor::SrcColor: return GPU_SRC_COLOR;
	case BlendFactor::OneMinusSrcColor: return GPU_ONE_MINUS_SRC_COLOR;
	case BlendFactor::DstColor: return GPU_DST_COLOR;
	case BlendFactor::OneMinusDstColor: return GPU_ONE_MINUS_DST_COLOR;
	case BlendFactor::SrcAlpha: return GPU_SRC_ALPHA;
	case BlendFactor::OneMinusSrcAlpha: return GPU_ONE_MINUS_SRC_ALPHA;
	case BlendFactor::DstAlpha: return GPU_DST_ALPHA;
	case BlendFactor::OneMinusDstAlpha: return GPU_ONE_MINUS_DST_ALPHA;
	case BlendFactor::SrcAlphaSaturate: return GPU_SRC_ALPHA_SATURATE;
	default: return GPU_ONE;
	}
}

static GPU_BLENDEQUATION blendEquation(redbound::BlendEquation e) {
	switch (e) {
	case BlendEquation::Subtract: return GPU_BLEND_SUBTRACT;
	case BlendEquation::ReverseSubtract: return GPU_BLEND_REVERSE_SUBTRACT;
	default: return GPU_BLEND_ADD;
	}
}

bool Citro3D_Effect::isWater() const {
	if (findTechnique("Water") >= 0) return true;
	const std::string b = baseName(name);
	return b.compare(0, 5, "water") == 0;
}

int Citro3D_Effect::findTechnique(const std::string& n) const {
	for (size_t i = 0; i < techniqueNames.size(); ++i)
		if (techniqueNames[i] == n) return (int)i;
	return -1;
}

Citro3D_Texture::Citro3D_Texture(Citro3D_Context* ctx, const String& filename) :
	m_owner(ctx), m_width(1), m_height(1), m_valid(false), m_renderTarget(false), m_bytes(0)
{
	if (m_owner) m_owner->registerTexture(this);
	redbound::Image img;
	const std::string path = filename.c_str();
	if (!redbound::loadImage(path, img) || img.levels.empty()) {
		printf("[citro3d] texture '%s': %s\n", path.c_str(), img.error.c_str());
		createChecker();
		++ctx->texturesFailed;
		return;
	}

	const int maxSize = ctx->maxTextureSize();
	RgbaLevel base;
	base.width = picaTextureSize(img.levels[0].width, maxSize);
	base.height = picaTextureSize(img.levels[0].height, maxSize);
	if (base.width == img.levels[0].width && base.height == img.levels[0].height) {
		base.rgba.swap(img.levels[0].rgba);
	} else {
		base = resampleRgba(&img.levels[0].rgba[0], img.levels[0].width, img.levels[0].height, base.width, base.height);
	}
	if (img.verticalFlipFlag != ctx->flipTextures) {
		const size_t row = (size_t)base.width * 4;
		std::vector<uint8_t> tmp(row);
		for (int y = 0; y < base.height / 2; ++y) {
			uint8_t* a = &base.rgba[(size_t)y * row];
			uint8_t* b = &base.rgba[(size_t)(base.height - 1 - y) * row];
			memcpy(&tmp[0], a, row);
			memcpy(a, b, row);
			memcpy(b, &tmp[0], row);
		}
	}

	char note[256];
	snprintf(note, sizeof(note), "tex %s %dx%d -> %dx%d %s levels %d flip %d", baseName(path).c_str(),
	         img.levels[0].width, img.levels[0].height, base.width, base.height,
	         img.hasAlpha ? "rgba8" : "rgb565", picaMipLevels(base.width, base.height), (int)img.verticalFlipFlag);
	ctx->report(note);
	const TexelFormat format = chooseTexelFormat(img.hasAlpha);
	const GPU_TEXCOLOR gpuFormat = format == TexelFormat::RGBA8 ? GPU_RGBA8 : GPU_RGB565;
	const int levels = picaMipLevels(base.width, base.height);
	if (!C3D_TexInitMipmap(&m_tex, (u16)base.width, (u16)base.height, gpuFormat)) {
		printf("[citro3d] out of texture memory for '%s' (%dx%d)\n", path.c_str(), base.width, base.height);
		createChecker();
		++ctx->texturesFailed;
		return;
	}
	m_width = base.width;
	m_height = base.height;
	m_valid = true;
	m_bytes = texelBytes(format) * (size_t)base.width * base.height * 4 / 3;
	ctx->textureBytes += m_bytes;

	RgbaLevel level = base;
	std::vector<uint8_t> tiled;
	for (int l = 0; l < levels; ++l) {
		if (l > 0) level = halveRgba(level);
		tiled.resize(texelBytes(format) * (size_t)level.width * level.height);
		tileLevel(&level.rgba[0], level.width, level.height, format, &tiled[0]);
		u32 size = 0;
		void* dst = C3D_Tex2DGetImagePtr(&m_tex, l, &size);
		if (!dst || size < tiled.size()) break;
		memcpy(dst, &tiled[0], tiled.size());
	}
	C3D_TexSetFilter(&m_tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetFilterMipmap(&m_tex, GPU_LINEAR);
	C3D_TexSetWrap(&m_tex, GPU_REPEAT, GPU_REPEAT);
	C3D_TexFlush(&m_tex);
	++ctx->texturesLoaded;
}

Citro3D_Texture::Citro3D_Texture(Citro3D_Context* ctx, int width, int height) :
	m_owner(ctx), m_width(width), m_height(height), m_valid(false), m_renderTarget(true), m_bytes(0)
{
	if (m_owner) m_owner->registerTexture(this);
	m_width = picaTextureSize(width, ctx->maxTextureSize());
	m_height = picaTextureSize(height, ctx->maxTextureSize());
	m_valid = C3D_TexInitVRAM(&m_tex, (u16)m_width, (u16)m_height, GPU_RGBA8);
	if (!m_valid) {
		printf("[citro3d] no VRAM for a %dx%d render target\n", m_width, m_height);
		return;
	}
	m_bytes = (size_t)m_width * m_height * 4;
	C3D_TexSetFilter(&m_tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&m_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
}

void Citro3D_Texture::createChecker() {
	m_width = m_height = 8;
	if (!C3D_TexInit(&m_tex, 8, 8, GPU_RGBA8)) return;
	m_valid = true;
	uint8_t px[8 * 8 * 4];
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x) {
			uint8_t* p = px + ((size_t)y * 8 + x) * 4;
			const bool c = (((x / 2) + (y / 2)) & 1) != 0;
			p[0] = c ? 255 : 90; p[1] = c ? 0 : 90; p[2] = c ? 255 : 90; p[3] = 255;
		}
	std::vector<uint8_t> tiled(8 * 8 * 4);
	tileLevel(px, 8, 8, TexelFormat::RGBA8, &tiled[0]);
	C3D_TexUpload(&m_tex, &tiled[0]);
	C3D_TexSetFilter(&m_tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexFlush(&m_tex);
}

Citro3D_Texture::~Citro3D_Texture() {
	if (m_owner) {
		m_owner->textureBytes -= std::min(m_bytes, m_owner->textureBytes);
		m_owner->unregisterTexture(this);
	}
	if (m_valid && !citro3dShuttingDown) C3D_TexDelete(&m_tex);
}

void Citro3D_Texture::blt(int x, int y, const void* data, int pitch, int w, int h, const SurfaceFormat& fmt, const void*, const SurfaceFormat&) {
	if (!m_valid || !data || fmt.type() != SurfaceFormat::SURFACE_A8R8G8B8) return;
	if (x != 0 || y != 0 || w != m_width || h != m_height) return;
	std::vector<uint8_t> rgba((size_t)w * h * 4);
	for (int j = 0; j < h; ++j) {
		const uint8_t* s = (const uint8_t*)data + (size_t)j * pitch;
		for (int i = 0; i < w; ++i) {
			uint8_t* d = &rgba[((size_t)j * w + i) * 4];
			d[0] = s[i * 4 + 2]; d[1] = s[i * 4 + 1]; d[2] = s[i * 4 + 0]; d[3] = s[i * 4 + 3];
		}
	}
	std::vector<uint8_t> tiled(rgba.size());
	tileLevel(&rgba[0], w, h, TexelFormat::RGBA8, &tiled[0]);
	C3D_TexUpload(&m_tex, &tiled[0]);
	C3D_TexFlush(&m_tex);
}

bool Citro3D_Context::writeReport(const std::string& path) const {
	FILE* f = fopen(path.c_str(), "w");
	if (!f) return false;
	fprintf(f, "textures %d ok %d failed, %u KB\n", texturesLoaded, texturesFailed, (unsigned)(textureBytes / 1024));
	fprintf(f, "effects %d ok %d fallback\n\n", effectsLoaded, effectsFallback);
	for (const std::string& line : m_report) fprintf(f, "%s\n", line.c_str());
	fclose(f);
	return true;
}

Citro3D_Context::Citro3D_Context(int width, int height) :
	time(0.f), fogRange(1e6f, 2e6f, 1e-6f, 0.f), fogColor(0.5f, 0.6f, 0.7f, 1.f),
	texturesLoaded(0), texturesFailed(0), effectsLoaded(0), effectsFallback(0), textureBytes(0),
	mirrored(false), flipTextures(false), invertCulling(false), disableAlphaTest(false),
	debugMode(0), meshLimit(0), meshesDrawn(0), combinerUse{ 0, 0, 0, 0 },
	m_width(width), m_height(height), m_platformId(5), m_maxTextureSize(512),
	m_viewport(0, 0, width, height), m_proj(1.f), m_target(0), m_targetWidth(width), m_targetHeight(height)
{
	m_shaderBinary = DVLB_ParseFile((u32*)scene_shbin, scene_shbin_size);
	shaderProgramInit(&m_program);
	shaderProgramSetVsh(&m_program, &m_shaderBinary->DVLE[0]);
	C3D_BindProgram(&m_program);

	static const char* const UNIFORMS[] = { "totalMtx", "worldMtx", "lightPos", "diffuseColor", "ambientColor", "fogRange", "fogColor", "texScale" };
	for (const char* n : UNIFORMS) m_uniforms[n] = shaderInstanceGetUniformLocation(m_program.vertexShader, n);

	C3D_AttrInfo* attr = C3D_GetAttrInfo();
	AttrInfo_Init(attr);
	AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attr, 2, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attr, 3, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attr, 4, GPU_FLOAT, 4);

	C3D_DepthMap(true, -1.f, 0.f);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_BACK_CCW);
}

Citro3D_Context::~Citro3D_Context() {
	for (Citro3D_Texture* t : m_textures) t->detachOwner();
	m_textures.clear();
	if (citro3dShuttingDown) return;
	for (auto& kv : m_neutral) {
		C3D_TexDelete(kv.second);
		delete kv.second;
	}
	shaderProgramFree(&m_program);
	DVLB_Free(m_shaderBinary);
}

int Citro3D_Context::uniformLocation(const char* name) const {
	const auto it = m_uniforms.find(name);
	return it == m_uniforms.end() ? -1 : it->second;
}

C3D_Tex* Citro3D_Context::neutralTexture(const std::string& sampler) {
	uint8_t c[4] = { 255, 255, 255, 255 };
	if (sampler == "REFLMAP" || sampler == "REFRACMAP") { c[0] = c[1] = c[2] = 0; }
	else if (sampler == "NORMALMAP") { c[0] = 128; c[1] = 128; c[2] = 255; }
	else if (sampler == "SIMAP") { c[0] = c[1] = c[2] = 128; }
	const std::string key(sampler == "REFLMAP" || sampler == "REFRACMAP" || sampler == "NORMALMAP" || sampler == "SIMAP" ? sampler : "WHITE");
	C3D_Tex*& tex = m_neutral[key];
	if (!tex) {
		tex = new C3D_Tex;
		if (!C3D_TexInit(tex, 8, 8, GPU_RGBA8)) {
			delete tex;
			tex = 0;
			return 0;
		}
		uint8_t px[8 * 8 * 4];
		for (int i = 0; i < 64; ++i) memcpy(px + i * 4, c, 4);
		std::vector<uint8_t> tiled(sizeof(px));
		tileLevel(px, 8, 8, TexelFormat::RGBA8, &tiled[0]);
		C3D_TexUpload(tex, &tiled[0]);
		C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
		C3D_TexFlush(tex);
	}
	return tex;
}

std::string Citro3D_Context::resolve(const std::string& dir, const std::string& name) const {
	std::string n = name;
	std::replace(n.begin(), n.end(), '\\', '/');
	if (fileExists(n)) return n;
	const std::string b = baseName(n);
	if (!dir.empty() && fileExists(redbound::joinPath(dir, b))) return redbound::joinPath(dir, b);
	return std::string();
}

Citro3D_Effect* Citro3D_Context::fallbackEffect() {
	if (!m_fallback) {
		m_fallback = new Citro3D_Effect;
		m_fallback->name = "(fallback)";
		m_fallback->priority = 0;
		m_fallback->sort = Shader::SORT_NONE;
		m_fallback->fallback = true;
		redbound::PassRecipe r;
		r.combiner = PassCombiner::Texture;
		r.baseSampler = "BASEMAP";
		r.samplers.push_back("BASEMAP");
		m_fallback->techniqueNames.push_back("Default");
		m_fallback->techniques.push_back(std::vector<redbound::PassRecipe>(1, r));
	}
	return m_fallback;
}

Citro3D_Effect* Citro3D_Context::loadEffect(const std::string& file, int flags) {
	char key[32];
	snprintf(key, sizeof(key), "#%d", flags);
	const std::string k = file + key;
	const auto it = m_effects.find(k);
	if (it != m_effects.end()) return it->second;

	redbound::Effect fx;
	std::vector<std::string> extra;
	if (!redbound::parseEffect(file, extra, fx)) {
		printf("[citro3d] effect '%s': %s\n", baseName(file).c_str(), fx.error.c_str());
		++effectsFallback;
		return m_effects[k] = fallbackEffect();
	}
	P(Citro3D_Effect) e = new Citro3D_Effect;
	e->name = file;
	e->fallback = false;
	e->priority = fx.globals.count("priority") ? atoi(fx.globals["priority"].c_str()) : 0;
	std::string sort = fx.globals.count("sort") ? fx.globals["sort"] : "";
	std::transform(sort.begin(), sort.end(), sort.begin(), ::toupper);
	e->sort = sort == "BACKTOFRONT" ? Shader::SORT_BACKTOFRONT : sort == "FRONTTOBACK" ? Shader::SORT_FRONTTOBACK :
	          sort == "INSIDEOUT" ? Shader::SORT_INSIDEOUT : sort == "OUTSIDEIN" ? Shader::SORT_OUTSIDEIN : Shader::SORT_NONE;

	const bool water = baseName(file).compare(0, 5, "water") == 0;
	for (size_t t = 0; t < fx.techniques.size(); ++t) {
		std::vector<redbound::PassRecipe> passes;
		for (size_t p = 0; p < fx.techniques[t].passes.size(); ++p) {
			const redbound::EffectPass& ep = fx.techniques[t].passes[p];
			std::string source, err;
			if (!redbound::buildShaderSource(ep.pixelShaderPath, fx.defines, true, source, err))
				printf("[citro3d] %s: %s\n", baseName(ep.pixelShaderPath).c_str(), err.c_str());
			const redbound::PassRecipe recipe = makePassRecipe(source, ep.states, water || fx.techniques[t].name == "Water");
			char note[384];
			std::string samplerList;
			for (const std::string& n : recipe.samplers) samplerList += (samplerList.empty() ? "" : "+") + n;
			snprintf(note, sizeof(note), "fx %s / %s / %s : %s base=%s lm=%s [%s] cull=%d depth=%d/%d blend=%d/%d/%d alpha=%d",
			         baseName(file).c_str(), fx.techniques[t].name.c_str(), ep.name.c_str(), combinerName(recipe.combiner),
			         recipe.baseSampler.empty() ? "-" : recipe.baseSampler.c_str(),
			         recipe.lightmapSampler.empty() ? "-" : recipe.lightmapSampler.c_str(),
			         samplerList.empty() ? "-" : samplerList.c_str(), (int)recipe.cull, (int)recipe.depthWrite,
			         (int)recipe.depthFunc, (int)recipe.blend, (int)recipe.blendSrc, (int)recipe.blendDst, (int)recipe.alphaTest);
			e->report.push_back(note);
			passes.push_back(recipe);
		}
		e->techniqueNames.push_back(fx.techniques[t].name);
		e->techniques.push_back(passes);
	}
	for (const std::string& line : e->report) report(line);
	++effectsLoaded;
	m_effects[k] = e;
	return e;
}

Shader* Citro3D_Context::createShader(const String& name, int flags) {
	std::string file = resolve(m_shaderDir, name.c_str());
	if (file.empty()) file = resolve(m_shaderDir, std::string(name.c_str()) + ".fx");
	Citro3D_Effect* e;
	if (file.empty()) {
		if (m_missingEffects.insert(name.c_str()).second)
			printf("[citro3d] effect '%s' not found\n", name.c_str());
		++effectsFallback;
		e = fallbackEffect();
	} else {
		e = loadEffect(file, flags);
	}
	return new Citro3D_Shader(this, e);
}

Primitive* Citro3D_Context::createPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices, UsageFlags) {
	return new Citro3D_Primitive(this, prim, vf, vertices, indices);
}

Primitive* Citro3D_Context::getDynamicPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices) {
	return new Citro3D_Primitive(this, prim, vf, vertices, indices);
}

Texture* Citro3D_Context::createTexture(int width, int height, const SurfaceFormat&, Palette*, int) {
	return new Citro3D_Texture(this, width, height);
}

Texture* Citro3D_Context::createTexture(const String& filename) {
	std::string f = resolve(m_textureDir, filename.c_str());
	if (f.empty()) {
		const std::string n = baseName(filename.c_str());
		const size_t dot = n.find_last_of('.');
		if (dot != std::string::npos) f = resolve(m_textureDir, n.substr(0, dot) + ".pvr");
	}
	if (f.empty()) f = filename.c_str();
	return new Citro3D_Texture(this, f.c_str());
}

void Citro3D_Context::setTarget(C3D_RenderTarget* target, int width, int height) {
	m_target = target;
	m_targetWidth = width;
	m_targetHeight = height;
	C3D_FrameDrawOn(target);
	m_viewport = Rect(0, 0, width, height);
}

void Citro3D_Context::setViewport(const Rect& r) {
	m_viewport = r;
	if (r.left() == 0 && r.top() == 0 && r.width() >= m_targetWidth && r.height() >= m_targetHeight) return;
	C3D_SetViewport((u32)r.top(), (u32)r.left(), (u32)r.height(), (u32)r.width());
}

void Citro3D_Context::setPerspectiveProjection(float hfov, float front, float back, float aspect) {
	m_proj.setPerspectiveProjection(hfov, front, back, aspect);
	for (int c = 0; c < 4; ++c)
		m_proj(2, c) = m_proj(2, c) - m_proj(3, c);
	for (int c = 0; c < 4; ++c) {
		const float x = m_proj(0, c), y = m_proj(1, c);
		m_proj(0, c) = y;
		m_proj(1, c) = -x;
	}
}

void Citro3D_Context::setOrthographicProjection(bool enabled) {
	if (!enabled) return;
	m_proj = float4x4(1.f);
	m_proj(0, 0) = 0.f; m_proj(0, 1) = 1.f;
	m_proj(1, 0) = -1.f; m_proj(1, 1) = 0.f;
}

void Citro3D_Context::setRenderTarget(Texture*) {}

void Citro3D_Context::clear(int color) {
	if (!m_target) return;
	const u32 rgba = (u32)(((color & 0xFF0000) << 8) | ((color & 0xFF00) << 8) | ((color & 0xFF) << 8) | 0xFF);
	C3D_RenderTargetClear(m_target, C3D_CLEAR_ALL, rgba, 0);
}

void Citro3D_Context::present(int) {}

const char* Citro3D_Shader::paramName(ParamType p) {
	switch (p) {
	case PARAM_CAMERAP: return "CAMERAP";
	case PARAM_WORLDTM: return "WORLDTM";
	case PARAM_VIEWTM: return "VIEWTM";
	case PARAM_VIEWTMINV: return "VIEWTMINV";
	case PARAM_VIEWPROJTM: return "VIEWPROJTM";
	case PARAM_PROJTM: return "PROJTM";
	case PARAM_TOTALTM: return "TOTALTM";
	case PARAM_BONEWORLDTM: return "BONEWORLDTM";
	default: break;
	}
	static const char* const LP[] = { "LIGHTP0", "LIGHTP1", "LIGHTP2", "LIGHTP3", "LIGHTP4", "LIGHTP5", "LIGHTP6", "LIGHTP7" };
	static const char* const LC[] = { "LIGHTC0", "LIGHTC1", "LIGHTC2", "LIGHTC3", "LIGHTC4", "LIGHTC5", "LIGHTC6", "LIGHTC7" };
	if (p >= PARAM_LIGHTP0 && p <= PARAM_LIGHTP7) return LP[p - PARAM_LIGHTP0];
	if (p >= PARAM_LIGHTC0 && p <= PARAM_LIGHTC7) return LC[p - PARAM_LIGHTC0];
	return "";
}

Citro3D_Shader::Citro3D_Shader(Citro3D_Context* ctx, Citro3D_Effect* fx) :
	m_ctx(ctx), m_fx(fx), m_technique(0), m_activeTechnique(0), m_sort(fx->sort), m_recipe(0)
{
}

Shader* Citro3D_Shader::clone() const {
	Citro3D_Shader* s = new Citro3D_Shader(m_ctx, m_fx);
	s->m_technique = m_technique;
	s->m_sort = m_sort;
	s->m_name = m_name;
	s->m_values = m_values;
	return s;
}

void Citro3D_Shader::setTechnique(const char* name) {
	const int i = m_fx->findTechnique(name);
	if (i >= 0) m_technique = i;
}

Citro3D_Shader::Value& Citro3D_Shader::value(const std::string& n, Value::Kind k) {
	Value& v = m_values[n];
	v.kind = k;
	return v;
}

void Citro3D_Shader::setTexture(ParamType p, BaseTexture* t) { setTexture(paramName(p), t); }
void Citro3D_Shader::setTexture(const char* name, BaseTexture* t) { value(name, Value::TEX).tex = t; }
void Citro3D_Shader::setMatrix(ParamType p, const float4x4& m) { setMatrix(paramName(p), m); }

void Citro3D_Shader::setMatrix(const char* name, const float4x4& m) {
	Value& v = value(name, Value::MAT);
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c) v.v[r * 4 + c] = m(r, c);
}

void Citro3D_Shader::setMatrixArray(ParamType, float4x4**, int) {}
void Citro3D_Shader::setVector(ParamType p, const float4& x) { setVector(paramName(p), x); }

void Citro3D_Shader::setVector(const char* name, const float4& x) {
	Value& v = value(name, Value::VEC);
	v.v[0] = x.x; v.v[1] = x.y; v.v[2] = x.z; v.v[3] = x.w;
}

void Citro3D_Shader::setFloat(ParamType p, float f) { setFloat(paramName(p), f); }

void Citro3D_Shader::setFloat(const char* name, float f) {
	Value& v = value(name, Value::VEC);
	v.v[0] = v.v[1] = v.v[2] = v.v[3] = f;
}

BaseTexture* Citro3D_Shader::getTexture(const char* name) {
	const auto it = m_values.find(name);
	return it != m_values.end() && it->second.kind == Value::TEX ? (BaseTexture*)it->second.tex : 0;
}

float4x4 Citro3D_Shader::getMatrix(const char* name) {
	const auto it = m_values.find(name);
	float4x4 m(1.f);
	if (it != m_values.end() && it->second.kind == Value::MAT)
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c) m(r, c) = it->second.v[r * 4 + c];
	return m;
}

float4 Citro3D_Shader::getVector(const char* name) {
	const auto it = m_values.find(name);
	if (it == m_values.end() || it->second.kind != Value::VEC) return float4(0, 0, 0, 0);
	return float4(it->second.v[0], it->second.v[1], it->second.v[2], it->second.v[3]);
}

float Citro3D_Shader::getFloat(const char* name) { return getVector(name).x; }

int Citro3D_Shader::begin() {
	m_activeTechnique = m_technique;
	if (!m_ctx->forcedTechnique.empty()) {
		m_activeTechnique = m_fx->findTechnique(m_ctx->forcedTechnique);
		if (m_fx->fallback) m_activeTechnique = 0;
		if (m_activeTechnique < 0) return 0;
	}
	return (int)m_fx->techniques[m_activeTechnique].size();
}

void Citro3D_Shader::applyStates(const redbound::PassRecipe& r) {
	const bool sky = materialIsSky(m_name.c_str());
	CullMode cull = sky ? CullMode::None : r.cull;
	if (m_ctx->mirrored) cull = cull == CullMode::Back ? CullMode::Front : cull == CullMode::Front ? CullMode::Back : cull;
	if (m_ctx->invertCulling) cull = cull == CullMode::Back ? CullMode::Front : cull == CullMode::Front ? CullMode::Back : cull;
	C3D_CullFace(cull == CullMode::None ? GPU_CULL_NONE : cull == CullMode::Front ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW);

	C3D_DepthTest(true, depthFunc(r.overlay ? CompareFunc::LessEqual : r.depthFunc, true),
	              r.depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);

	float offset = 0.f;
	switch (materialDepthBias(m_name.c_str(), r)) {
	case DepthBias::Overlay: offset = 0.0025f; break;
	case DepthBias::DecalNear: offset = 0.0006f; break;
	case DepthBias::DecalFar: offset = 0.0012f; break;
	default: break;
	}
	C3D_DepthMap(true, -1.f, offset);

	if (r.blend) {
		C3D_AlphaBlend(blendEquation(r.blendEquation), blendEquation(r.blendEquation),
		               blendFactor(r.blendSrc), blendFactor(r.blendDst), blendFactor(r.blendSrc), blendFactor(r.blendDst));
	} else {
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	}
	C3D_AlphaTest(r.alphaTest && !r.blend && !m_ctx->disableAlphaTest, GPU_GREATER, 127);
}

static bool isAuxiliary(const std::string& name) {
	return name == "SIMAP" || name == "NORMALMAP" || name == "REFLMAP" || name == "REFRACMAP" ||
	       name == "DEPTHMAP" || name == "SHININESSMAP" || name == "GLOSSINESSMAP";
}

redbound::PassCombiner Citro3D_Shader::resolveSamplers(const redbound::PassRecipe& r, std::string& base, std::string& lightmap) const {
	std::vector<std::string> provided;
	for (const auto& kv : m_values)
		if (kv.second.kind == Value::TEX && kv.second.tex) provided.push_back(kv.first);

	lightmap.clear();
	base.clear();
	for (const std::string& n : provided)
		if (n == "SIMAP") lightmap = n;
	static const char* const BASE_NAMES[] = { "BASEMAP", "DIFFUSEMAP" };
	for (const char* n : BASE_NAMES)
		if (base.empty() && std::find(provided.begin(), provided.end(), n) != provided.end()) base = n;
	for (const std::string& n : provided)
		if (base.empty() && !isAuxiliary(n)) base = n;

	if (r.combiner == PassCombiner::Water) return PassCombiner::Water;
	if (base.empty() && lightmap.empty()) return PassCombiner::VertexColor;
	if (base.empty()) {
		base = lightmap;
		lightmap.clear();
		return PassCombiner::Texture;
	}
	return lightmap.empty() ? PassCombiner::Texture : PassCombiner::Lightmap;
}

void Citro3D_Shader::applyCombiner(redbound::PassCombiner combiner) {
	C3D_TexEnv* first = C3D_GetTexEnv(0);
	C3D_TexEnvInit(first);
	C3D_TexEnv* second = C3D_GetTexEnv(1);
	C3D_TexEnvInit(second);
	for (int i = 2; i < 6; ++i) {
		C3D_TexEnv* env = C3D_GetTexEnv(i);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}

	switch (combiner) {
	case PassCombiner::VertexColor:
		C3D_TexEnvSrc(first, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(first, C3D_Both, GPU_REPLACE);
		C3D_TexEnvSrc(second, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(second, C3D_Both, GPU_REPLACE);
		break;
	case PassCombiner::Lightmap:
		C3D_TexEnvSrc(first, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(first, C3D_Both, GPU_MODULATE);
		C3D_TexEnvSrc(second, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE1, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(second, C3D_RGB, GPU_ADD_SIGNED);
		C3D_TexEnvScale(second, C3D_RGB, GPU_TEVSCALE_2);
		C3D_TexEnvSrc(second, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(second, C3D_Alpha, GPU_REPLACE);
		break;
	case PassCombiner::Water:
		C3D_TexEnvSrc(first, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(first, C3D_Both, GPU_MODULATE);
		C3D_TexEnvSrc(second, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(second, C3D_Both, GPU_REPLACE);
		break;
	default:
		C3D_TexEnvSrc(first, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(first, C3D_Both, GPU_MODULATE);
		C3D_TexEnvSrc(second, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
		C3D_TexEnvFunc(second, C3D_Both, GPU_REPLACE);
		break;
	}
}

static void setRows(C3D_Mtx& out, const float* rowMajor) {
	for (int row = 0; row < 4; ++row) {
		const float x = rowMajor ? rowMajor[row * 4 + 0] : (row == 0 ? 1.f : 0.f);
		const float y = rowMajor ? rowMajor[row * 4 + 1] : (row == 1 ? 1.f : 0.f);
		const float z = rowMajor ? rowMajor[row * 4 + 2] : (row == 2 ? 1.f : 0.f);
		const float w = rowMajor ? rowMajor[row * 4 + 3] : (row == 3 ? 1.f : 0.f);
		out.r[row].x = x;
		out.r[row].y = y;
		out.r[row].z = z;
		out.r[row].w = w;
	}
}

void Citro3D_Shader::applyUniforms(const redbound::PassRecipe& r, redbound::PassCombiner combiner, const std::string& base, const std::string& lightmap) {
	C3D_Mtx m;
	const Value* total = 0;
	const Value* world = 0;
	const auto t = m_values.find("TOTALTM");
	if (t != m_values.end() && t->second.kind == Value::MAT) total = &t->second;
	const auto w = m_values.find("WORLDTM");
	if (w != m_values.end() && w->second.kind == Value::MAT) world = &w->second;

	setRows(m, total ? total->v : 0);
	if (m_ctx->uniformLocation("totalMtx") >= 0) C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, m_ctx->uniformLocation("totalMtx"), &m);

	setRows(m, world ? world->v : 0);
	if (m_ctx->uniformLocation("worldMtx") >= 0) C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, m_ctx->uniformLocation("worldMtx"), &m);

	const float4 light = getVector("LIGHTP0");
	if (m_ctx->uniformLocation("lightPos") >= 0)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, m_ctx->uniformLocation("lightPos"), light.x, light.y, light.z, 1.f);

	float4 diffuse = getVector("DIFFUSEC");
	if (m_values.find("DIFFUSEC") == m_values.end()) diffuse = float4(1.f, 1.f, 1.f, 1.f);
	if (m_ctx->debugMode == 2) diffuse = float4(0.f, 0.f, 0.f, 1.f);
	if (m_ctx->uniformLocation("diffuseColor") >= 0)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, m_ctx->uniformLocation("diffuseColor"), diffuse.x * 0.55f, diffuse.y * 0.55f, diffuse.z * 0.55f, 1.f);
	if (m_ctx->uniformLocation("ambientColor") >= 0)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, m_ctx->uniformLocation("ambientColor"), m_ctx->debugMode == 2 ? 1.f : 0.45f,
		              m_ctx->debugMode == 2 ? 1.f : 0.45f, m_ctx->debugMode == 2 ? 1.f : 0.45f, 1.f);
	if (m_ctx->uniformLocation("fogRange") >= 0)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, m_ctx->uniformLocation("fogRange"), m_ctx->fogRange.x, m_ctx->fogRange.y, m_ctx->fogRange.z, m_ctx->fogRange.w);
	if (m_ctx->uniformLocation("fogColor") >= 0)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, m_ctx->uniformLocation("fogColor"), m_ctx->fogColor.x, m_ctx->fogColor.y, m_ctx->fogColor.z, 1.f);
	if (m_ctx->uniformLocation("texScale") >= 0)
		C3D_FVUnifSet(GPU_VERTEX_SHADER, m_ctx->uniformLocation("texScale"), 1.f, 1.f, 1.f, 1.f);

	std::string units[2] = { base, lightmap };
	if (m_ctx->debugMode == 3 && !lightmap.empty()) {
		units[0] = lightmap;
		units[1].clear();
	}
	(void)combiner;
	(void)r;
	for (int i = 0; i < 2; ++i) {
		if (units[i].empty()) continue;
		Citro3D_Texture* tex = 0;
		if (m_ctx->debugMode == 1) {
			if (C3D_Tex* white = m_ctx->neutralTexture("WHITE")) C3D_TexBind(i, white);
			continue;
		}
		const auto v = m_values.find(units[i]);
		if (v != m_values.end() && v->second.kind == Value::TEX && v->second.tex)
			tex = dynamic_cast<Citro3D_Texture*>((BaseTexture*)v->second.tex);
		if (!tex && units[i] == "REFLMAP" && m_ctx->reflectionTexture)
			tex = dynamic_cast<Citro3D_Texture*>((Texture*)m_ctx->reflectionTexture);
		if (!tex) {
			const std::string key = m_fx->name + "|" + units[i];
			if (units[i] != "REFLMAP" && units[i] != "REFRACMAP" && m_ctx->warnedSamplers.insert(key).second)
				printf("[citro3d] sampler '%s' has no texture in '%s'\n", units[i].c_str(), baseName(m_fx->name).c_str());
		}
		C3D_Tex* handle = tex ? tex->handle() : 0;
		if (!handle) handle = m_ctx->neutralTexture(units[i]);
		if (handle) C3D_TexBind(i, handle);
	}
}

void Citro3D_Shader::beginPass(int pass) {
	const redbound::PassRecipe& r = m_fx->techniques[m_activeTechnique][pass];
	m_recipe = &r;
	std::string base, lightmap;
	redbound::PassCombiner combiner = resolveSamplers(r, base, lightmap);
	if (m_ctx->debugMode == 3 && !lightmap.empty()) combiner = PassCombiner::Texture;
	++m_ctx->combinerUse[(int)combiner];
	applyStates(r);
	applyCombiner(combiner);
	applyUniforms(r, combiner, base, lightmap);
}

void Citro3D_Shader::endPass() { m_recipe = 0; }
void Citro3D_Shader::end() {}

Citro3D_Primitive::Citro3D_Primitive(Citro3D_Context* ctx, PrimType type, const VertexFormat& vf, int vertices, int indices) :
	m_ctx(ctx), m_type(type), m_vertices(0), m_indices(0), m_vertexCount(0), m_indexCount(0), m_dirty(true)
{
	setFormat(vf, vertices, indices);
}

Citro3D_Primitive::~Citro3D_Primitive() {
	if (citro3dShuttingDown) return;
	linearFree(m_vertices);
	linearFree(m_indices);
}

void Citro3D_Primitive::unlock() {
	if (locked() & LOCK_WRITE) m_dirty = true;
	DIPrimitive::unlock();
}

void Citro3D_Primitive::upload() {
	const int n = vertices();
	if (n <= 0) return;
	if (n != m_vertexCount) {
		linearFree(m_vertices);
		m_vertices = (Vertex*)linearAlloc(sizeof(Vertex) * (size_t)n);
		m_vertexCount = m_vertices ? n : 0;
	}
	if (!m_vertices) return;
	memset(m_vertices, 0, sizeof(Vertex) * (size_t)m_vertexCount);

	lock(LOCK_READ);
	std::vector<float4> tmp((size_t)n);
	const float4 psb = vertexPositionScaleBias();
	const float4 tsb = vertexTextureCoordinateScaleBias();

	getVertexPositions(0, &tmp[0], n);
	for (int i = 0; i < n; ++i) {
		m_vertices[i].position[0] = tmp[i].x * psb.x + psb.y;
		m_vertices[i].position[1] = tmp[i].y * psb.x + psb.z;
		m_vertices[i].position[2] = tmp[i].z * psb.x + psb.w;
		m_vertices[i].color[0] = m_vertices[i].color[1] = m_vertices[i].color[2] = m_vertices[i].color[3] = 1.f;
		m_vertices[i].normal[1] = 1.f;
	}
	if (vertexFormat().hasData(VertexFormat::DT_NORMAL)) {
		getVertexNormals(0, &tmp[0], n);
		for (int i = 0; i < n; ++i) {
			m_vertices[i].normal[0] = tmp[i].x;
			m_vertices[i].normal[1] = tmp[i].y;
			m_vertices[i].normal[2] = tmp[i].z;
		}
	}
	if (vertexFormat().hasData(VertexFormat::DT_DIFFUSE)) {
		getVertexDiffuseColors(0, &tmp[0], n);
		for (int i = 0; i < n; ++i) {
			m_vertices[i].color[0] = tmp[i].x;
			m_vertices[i].color[1] = tmp[i].y;
			m_vertices[i].color[2] = tmp[i].z;
			m_vertices[i].color[3] = tmp[i].w;
		}
	}
	const bool hasTex0 = vertexFormat().hasData(VertexFormat::DT_TEX0);
	if (hasTex0) {
		getVertexTextureCoordinates(0, 0, &tmp[0], n);
		for (int i = 0; i < n; ++i) {
			m_vertices[i].texCoord0[0] = tmp[i].x * tsb.x + tsb.y;
			m_vertices[i].texCoord0[1] = tmp[i].y * tsb.x + tsb.z;
			m_vertices[i].texCoord1[0] = m_vertices[i].texCoord0[0];
			m_vertices[i].texCoord1[1] = m_vertices[i].texCoord0[1];
		}
	}
	if (vertexFormat().hasData(VertexFormat::DT_TEX1)) {
		getVertexTextureCoordinates(0, 1, &tmp[0], n);
		for (int i = 0; i < n; ++i) {
			m_vertices[i].texCoord1[0] = tmp[i].x * tsb.x + tsb.y;
			m_vertices[i].texCoord1[1] = tmp[i].y * tsb.x + tsb.z;
		}
	}

	if (indices() > 0) {
		if (indices() != m_indexCount) {
			linearFree(m_indices);
			m_indices = (uint16_t*)linearAlloc(sizeof(uint16_t) * (size_t)indices());
			m_indexCount = m_indices ? indices() : 0;
		}
		if (m_indices) {
			uint16_t* idx = 0;
			int isize = 0;
			getIndexDataPtr(&idx, &isize);
			memcpy(m_indices, idx, sizeof(uint16_t) * (size_t)m_indexCount);
		}
	}
	DIPrimitive::unlock();
	m_dirty = false;
}

void Citro3D_Primitive::sortTriangles() {
	Citro3D_Shader* fx = dynamic_cast<Citro3D_Shader*>((Shader*)m_shader);
	if (!fx || m_indexCount < 3 || !m_indices || m_type != PRIM_TRI) return;
	const Shader::SortType s = fx->sort();
	if (s != Shader::SORT_BACKTOFRONT && s != Shader::SORT_FRONTTOBACK) return;
	const float4 cam = fx->getVector("CAMERAP");
	const float4x4 world = fx->getMatrix("WORLDTM");
	lock(LOCK_READWRITE);
	if (s == Shader::SORT_BACKTOFRONT) sortBackToFront(float3(cam.x, cam.y, cam.z), world, 0, 0, m_sortBuffer);
	else sortFrontToBack(float3(cam.x, cam.y, cam.z), world, 0, 0, m_sortBuffer);
	uint16_t* idx = 0;
	int isize = 0;
	getIndexDataPtr(&idx, &isize);
	memcpy(m_indices, idx, sizeof(uint16_t) * (size_t)m_indexCount);
	DIPrimitive::unlock();
}

void Citro3D_Primitive::render() {
	if (m_dirty) upload();
	if (!m_vertices) return;
	++m_ctx->meshesDrawn;
	if (m_ctx->meshLimit > 0 && m_ctx->meshesDrawn > m_ctx->meshLimit) return;
	sortTriangles();

	C3D_BufInfo* buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, m_vertices, sizeof(Vertex), 5, 0x43210);

	GPU_Primitive_t mode;
	switch (m_type) {
	case PRIM_TRISTRIP: mode = GPU_TRIANGLE_STRIP; break;
	case PRIM_TRIFAN: mode = GPU_TRIANGLE_FAN; break;
	case PRIM_POINT: case PRIM_SPRITE: case PRIM_LINE: case PRIM_LINESTRIP: return;
	default: mode = GPU_TRIANGLES; break;
	}

	if (m_indexCount > 0 && m_indices) {
		const int ib = indexRangeBegin(), ie = indexRangeEnd();
		if (ie > ib) C3D_DrawElements(mode, ie - ib, C3D_UNSIGNED_SHORT, m_indices + ib);
	} else {
		const int vb = vertexRangeBegin(), ve = vertexRangeEnd();
		if (ve > vb) C3D_DrawArrays(mode, vb, ve - vb);
	}
}

END_NAMESPACE()
