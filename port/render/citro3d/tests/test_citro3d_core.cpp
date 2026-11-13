#include "PassRecipe.h"
#include "TextureTiling.h"
#include <cstdio>
#include <set>
using namespace redbound;
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char* LIGHTMAP_PS =
	"precision mediump float;\n"
	"uniform lowp sampler2D BASEMAP;\n"
	"uniform  lowp   sampler2D SIMAP;\n"
	"varying mediump vec2 V_TexCoord0;\n"
	"void main() { vec4 c = texture2D(BASEMAP, V_TexCoord0); c.rgb += (texture2D(SIMAP, V_TexCoord0).rgb - 0.5) * 2.0; gl_FragColor = c; }\n";

static const char* CUTOUT_PS =
	"uniform sampler2D LEAFMAP;\n"
	"uniform sampler2D NORMALMAP;\n"
	"void main() { vec4 c = texture2D(LEAFMAP, vec2(0.0)); if (c.a < 0.5) discard; gl_FragColor = c; }\n";

static const char* COMMON_HEADER_PS =
	"uniform sampler2D BASEMAP;\n"
	"uniform sampler2D NORMALMAP;\n"
	"uniform sampler2D SIMAP;\n"
	"uniform sampler2D REFLMAP;\n"
	"uniform sampler2D GLOSSINESSMAP;\n"
	"varying mediump vec2 V_TexCoord0;\n"
	"void main() {\n"
	"  lowp vec4 c = texture2D(BASEMAP, V_TexCoord0);\n"
	"  c.rgb += (texture2D( SIMAP , V_TexCoord0).rgb - 0.5) * 2.0;\n"
	"  gl_FragColor = c;\n"
	"}\n";

int main() {
	printf("sampler detection\n");
	std::vector<std::string> s = pixelSamplers(LIGHTMAP_PS);
	CHECK(s.size() == 2 && s[0] == "BASEMAP" && s[1] == "SIMAP");
	CHECK(pixelSamplers("vec4 sampler2D_helper; uniform sampler2D A; uniform sampler2D A;").size() == 1);
	CHECK(pixelSamplers("// texture2D only\nvoid main() {}").empty());

	printf("only sampled textures count, not every declaration\n");
	std::vector<std::string> used = sampledTextures(COMMON_HEADER_PS);
	CHECK(used.size() == 2 && used[0] == "BASEMAP" && used[1] == "SIMAP");
	CHECK(pixelSamplers(COMMON_HEADER_PS).size() == 5);
	CHECK(sampledTextures("uniform sampler2D A; void main() { mytexture2D(A, v); }").empty());
	CHECK(sampledTextures("uniform sampler2D A; void main() { texture2D(UNDECLARED, v); }").empty());
	CHECK(sampledTextures("uniform sampler2D A;\nvoid main() { texture2DProj(A, v); }").size() == 1);
	PassRecipe common = makePassRecipe(COMMON_HEADER_PS, {}, false);
	CHECK(common.combiner == PassCombiner::Lightmap && common.samplers.size() == 2);
	PassRecipe plain = makePassRecipe(
		"uniform sampler2D BASEMAP;\nuniform sampler2D SIMAP;\nvoid main() { gl_FragColor = texture2D(BASEMAP, v); }", {}, false);
	CHECK(plain.combiner == PassCombiner::Texture && plain.lightmapSampler.empty());

	printf("combiner choice\n");
	std::map<std::string, std::string> none;
	PassRecipe r = makePassRecipe(LIGHTMAP_PS, none, false);
	CHECK(r.combiner == PassCombiner::Lightmap && r.baseSampler == "BASEMAP" && r.lightmapSampler == "SIMAP" && !r.alphaTest);
	r = makePassRecipe(CUTOUT_PS, none, false);
	CHECK(r.combiner == PassCombiner::Texture && r.baseSampler == "LEAFMAP" && r.alphaTest);
	r = makePassRecipe("uniform sampler2D REFLMAP; uniform sampler2D NORMALMAP;", none, true);
	CHECK(r.combiner == PassCombiner::Water && r.blend && r.blendSrc == BlendFactor::SrcAlpha);
	r = makePassRecipe("void main() {}", none, false);
	CHECK(r.combiner == PassCombiner::VertexColor);

	printf("render states\n");
	std::map<std::string, std::string> st = {
		{ "EnableCulling", "false" }, { "enableblending", "true" }, { "blendfuncsrc", "src_alpha" },
		{ "blendfuncdst", "ONE" }, { "enabledepthmask", "FALSE" }, { "depthfunc", "lequal" } };
	r = makePassRecipe(LIGHTMAP_PS, st, false);
	CHECK(r.cull == CullMode::None && r.blend && r.blendSrc == BlendFactor::SrcAlpha && r.blendDst == BlendFactor::One);
	CHECK(!r.depthWrite && !r.overlay && r.depthFunc == CompareFunc::LessEqual);
	r = makePassRecipe(LIGHTMAP_PS, { { "enabledepthtest", "FALSE" }, { "cullface", "FRONT" } }, false);
	CHECK(r.overlay && !r.depthWrite && r.cull == CullMode::Front);
	CHECK(materialDepthBias("eyes", r) == DepthBias::Overlay);
	r = makePassRecipe(LIGHTMAP_PS, none, false);
	CHECK(materialDepthBias("rail_boost", r) == DepthBias::DecalNear && materialDepthBias("boost_glow", r) == DepthBias::DecalFar);
	CHECK(materialDepthBias("grass", r) == DepthBias::None);
	CHECK(materialIsSky("SkyBox01") && !materialIsSky("sky"));

	printf("texture sizes and mip levels\n");
	CHECK(picaTextureSize(1, 1024) == 8 && picaTextureSize(64, 1024) == 64 && picaTextureSize(65, 1024) == 128);
	CHECK(picaTextureSize(2048, 1024) == 1024 && picaTextureSize(300, 256) == 256);
	for (int w = 8; w <= 1024; w <<= 1)
		for (int h = 8; h <= 1024; h <<= 1) {
			const int m = w < h ? w : h;
			const int citro = (31 - __builtin_clz((unsigned)m)) - 3;
			CHECK(picaMipLevels(w, h) == citro + 1);
		}

	printf("morton order covers every texel of a tile once\n");
	std::set<uint32_t> seen;
	for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) seen.insert(mortonIndex(x, y));
	CHECK(seen.size() == 64 && *seen.rbegin() == 63);
	CHECK(mortonIndex(1, 0) == 1 && mortonIndex(0, 1) == 2 && mortonIndex(2, 0) == 4 && mortonIndex(7, 7) == 63);

	printf("tiling round trip\n");
	const int W = 16, H = 8;
	std::vector<uint8_t> rgba(W * H * 4);
	for (int i = 0; i < W * H; ++i) { rgba[i * 4] = (uint8_t)i; rgba[i * 4 + 1] = (uint8_t)(i * 3); rgba[i * 4 + 2] = (uint8_t)(255 - i); rgba[i * 4 + 3] = (uint8_t)(i * 7); }
	std::vector<uint8_t> tiled(W * H * 4);
	tileLevel(&rgba[0], W, H, TexelFormat::RGBA8, &tiled[0]);
	bool ok = true;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			const size_t idx = ((size_t)(y / 8) * (W / 8) + x / 8) * 64 + mortonIndex(x & 7, y & 7);
			const uint8_t* t = &tiled[idx * 4];
			const uint8_t* p = &rgba[((size_t)y * W + x) * 4];
			if (t[0] != p[3] || t[1] != p[2] || t[2] != p[1] || t[3] != p[0]) ok = false;
		}
	CHECK(ok);
	std::vector<uint8_t> px = { 255, 0, 0, 255 };
	std::vector<uint8_t> big(8 * 8 * 4);
	for (int i = 0; i < 64; ++i) for (int c = 0; c < 4; ++c) big[i * 4 + c] = px[c];
	std::vector<uint8_t> t565(8 * 8 * 2), t4(8 * 8 * 2);
	tileLevel(&big[0], 8, 8, TexelFormat::RGB565, &t565[0]);
	tileLevel(&big[0], 8, 8, TexelFormat::RGBA4, &t4[0]);
	CHECK(t565[0] == 0x00 && t565[1] == 0xF8);
	CHECK(t4[0] == 0x0F && t4[1] == 0xF0);

	printf("resampling\n");
	RgbaLevel half = halveRgba(RgbaLevel{ 2, 2, { 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255 } });
	CHECK(half.width == 1 && half.height == 1 && half.rgba[0] == 128);
	RgbaLevel up = resampleRgba(&px[0], 1, 1, 8, 8);
	CHECK(up.width == 8 && up.rgba.size() == 256 && up.rgba[252] == 255 && up.rgba[255] == 255);
	CHECK(chooseTexelFormat(true) == TexelFormat::RGBA8 && chooseTexelFormat(false) == TexelFormat::RGB565);

	printf("%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
