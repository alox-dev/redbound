#pragma once
#include "EffectParser.h"
#include "PassRecipe.h"
#include <gr/Context.h>
#include <gr/CubeTexture.h>
#include <gr/Palette.h>
#include <gr/Rect.h>
#include <gr/Shader.h>
#include <gr/SurfaceFormat.h>
#include <gr/Texture.h>
#include <gr/impl/DIPrimitive.h>
#include <gr/impl/SortBuffer.h>
#include <lang/String.h>
#include <math/float4x4.h>
#include <citro3d.h>
#include <map>
#include <set>
#include <string>
#include <vector>

BEGIN_NAMESPACE(gr)

extern bool citro3dShuttingDown;

class Citro3D_Context;

class Citro3D_Texture : public Texture {
public:
	Citro3D_Texture(Citro3D_Context* ctx, const lang::String& filename);
	Citro3D_Texture(Citro3D_Context* ctx, int width, int height);
	~Citro3D_Texture();

	void blt(int x, int y, const void* data, int pitch, int w, int h, const SurfaceFormat& fmt, const void* pal, const SurfaceFormat& palfmt);
	void clear() {}
	int width() const { return m_width; }
	int height() const { return m_height; }
	Rect rect() const { return Rect(0, 0, m_width, m_height); }
	SurfaceFormat format() const { return SurfaceFormat(SurfaceFormat::SURFACE_A8R8G8B8); }
	void getData(void** bits, int* pitch) const { *bits = 0; *pitch = 0; }

	C3D_Tex* handle() { return m_valid ? &m_tex : 0; }
	bool renderTarget() const { return m_renderTarget; }
	void detachOwner() { m_owner = 0; }
	size_t bytes() const { return m_bytes; }

private:
	Citro3D_Context* m_owner;
	C3D_Tex m_tex;
	int m_width, m_height;
	bool m_valid, m_renderTarget;
	size_t m_bytes;

	void createChecker();
};

class Citro3D_CubeTexture : public CubeTexture {
public:
	int width() const { return 1; }
	int height() const { return 1; }
	SurfaceFormat format() const { return SurfaceFormat(SurfaceFormat::SURFACE_A8R8G8B8); }
};

class Citro3D_Palette : public Palette {
public:
	explicit Citro3D_Palette(int n) : m_n(n) {}
	void setEntries(int, const void*, const SurfaceFormat&, int) {}
	void getEntries(int, void*, const SurfaceFormat&, int) {}
	int entries() const { return m_n; }
private:
	int m_n;
};

class Citro3D_Effect : public lang::Object {
public:
	std::string name;
	int priority;
	Shader::SortType sort;
	std::vector<std::string> techniqueNames;
	std::vector<std::vector<redbound::PassRecipe> > techniques;
	std::vector<std::string> report;
	bool fallback;

	bool isWater() const;
	int findTechnique(const std::string& n) const;
};

class Citro3D_Shader : public Shader {
public:
	Citro3D_Shader(Citro3D_Context* ctx, Citro3D_Effect* fx);
	Shader* clone() const;

	void setTechnique(const char* name);
	void setTexture(ParamType param, BaseTexture* value);
	void setTexture(const char* name, BaseTexture* value);
	void setMatrix(ParamType param, const math::float4x4& value);
	void setMatrix(const char* name, const math::float4x4& value);
	void setMatrixArray(ParamType param, math::float4x4** values, int count);
	void setVector(ParamType param, const math::float4& value);
	void setVector(const char* name, const math::float4& value);
	void setFloat(ParamType param, float value);
	void setFloat(const char* name, float value);
	BaseTexture* getTexture(const char* name);
	math::float4x4 getMatrix(const char* name);
	math::float4 getVector(const char* name);
	float getFloat(const char* name);
	int begin();
	void beginPass(int pass);
	void endPass();
	void end();
	void setName(const lang::String& name) { m_name = name; }
	void setSort(SortType sort) { m_sort = sort; }
	int priority() const { return m_fx->priority; }
	SortType sort() const { return m_sort; }
	bool enabled() const { return true; }
	const lang::String& name() const { return m_name; }

	static const char* paramName(ParamType p);
	const redbound::PassRecipe* currentRecipe() const { return m_recipe; }

private:
	struct Value {
		enum Kind { VEC, MAT, TEX } kind;
		float v[16];
		P(BaseTexture) tex;
	};

	Citro3D_Context* m_ctx;
	P(Citro3D_Effect) m_fx;
	int m_technique, m_activeTechnique;
	SortType m_sort;
	lang::String m_name;
	std::map<std::string, Value> m_values;
	const redbound::PassRecipe* m_recipe;

	Value& value(const std::string& n, Value::Kind k);
	redbound::PassCombiner resolveSamplers(const redbound::PassRecipe& r, std::string& base, std::string& lightmap) const;
	void applyStates(const redbound::PassRecipe& r);
	void applyCombiner(redbound::PassCombiner combiner);
	void applyUniforms(const redbound::PassRecipe& r, redbound::PassCombiner combiner, const std::string& base, const std::string& lightmap);
};

class Citro3D_Primitive : public DIPrimitive {
public:
	Citro3D_Primitive(Citro3D_Context* ctx, PrimType type, const VertexFormat& vf, int vertices, int indices);
	~Citro3D_Primitive();
	void setShader(Shader* fx) { m_shader = fx; }
	Shader* shader() const { return m_shader; }
	PrimType type() const { return m_type; }
	void unlock();
	void render();

private:
	struct Vertex {
		float position[3];
		float texCoord0[2];
		float texCoord1[2];
		float normal[3];
		float color[4];
	};

	Citro3D_Context* m_ctx;
	PrimType m_type;
	P(Shader) m_shader;
	Vertex* m_vertices;
	uint16_t* m_indices;
	int m_vertexCount, m_indexCount;
	bool m_dirty;
	SortBuffer m_sortBuffer;

	void upload();
	void sortTriangles();
};

class Citro3D_Context : public Context {
public:
	Citro3D_Context(int width, int height);
	~Citro3D_Context();

	void setShaderDir(const std::string& d) { m_shaderDir = d; }
	void setTextureDir(const std::string& d) { m_textureDir = d; }
	void setPlatformId(int id) { m_platformId = id; }
	void setMaxTextureSize(int size) { m_maxTextureSize = size; }
	int maxTextureSize() const { return m_maxTextureSize; }

	Shader* createShader(const lang::String& name, int flags = 0);
	Palette* createPalette(int entries) { return new Citro3D_Palette(entries); }
	Primitive* createPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices, UsageFlags usage = USAGE_STATIC);
	Primitive* getDynamicPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices);
	Texture* createTexture(int width, int height, const SurfaceFormat& fmt, Palette* pal, int usageflags);
	Texture* createTexture(const lang::String& filename);
	CubeTexture* createCubeTexture(const lang::String&) { return new Citro3D_CubeTexture; }
	void setViewport(const Rect& rect);
	void setPerspectiveProjection(float hfov, float front, float back, float aspect);
	void setOrthographicProjection(bool enabled);
	void setRenderTarget(Texture* target);
	void beginScene() {}
	void endScene() {}
	void clear(int color = 0);
	void present(int color = 0);
	bool ready() { return true; }
	void capture(const lang::String&) {}
	int width() const { return m_width; }
	int height() const { return m_height; }
	SurfaceFormat surfaceFormat() const { return SurfaceFormat(SurfaceFormat::SURFACE_A8R8G8B8); }
	const Rect& viewport() const { return m_viewport; }
	const math::float4x4& projectionTransform() const { return m_proj; }
	PlatformType platform() const { return (PlatformType)m_platformId; }
	OrientationType orientation() const { return ORIENTATION_0; }

	void setTarget(C3D_RenderTarget* target, int width, int height);
	int uniformLocation(const char* name) const;
	C3D_Tex* neutralTexture(const std::string& sampler);
	void report(const std::string& line) { m_report.push_back(line); }
	bool writeReport(const std::string& path) const;
	void registerTexture(Citro3D_Texture* t) { m_textures.insert(t); }
	void unregisterTexture(Citro3D_Texture* t) { m_textures.erase(t); }

	float time;
	math::float4 fogRange, fogColor;
	int texturesLoaded, texturesFailed, effectsLoaded, effectsFallback;
	size_t textureBytes;
	std::set<std::string> warnedSamplers;
	bool mirrored;
	bool flipTextures;
	bool invertCulling;
	bool disableAlphaTest;
	int debugMode;
	int meshLimit;
	int meshesDrawn;
	int combinerUse[4];
	std::string forcedTechnique;
	P(Texture) reflectionTexture;
	P(Texture) waterNormal;

private:
	int m_width, m_height, m_platformId, m_maxTextureSize;
	Rect m_viewport;
	math::float4x4 m_proj;
	std::string m_shaderDir, m_textureDir;
	std::map<std::string, P(Citro3D_Effect)> m_effects;
	P(Citro3D_Effect) m_fallback;
	std::set<std::string> m_missingEffects;
	std::set<Citro3D_Texture*> m_textures;
	std::map<std::string, C3D_Tex*> m_neutral;
	C3D_RenderTarget* m_target;
	int m_targetWidth, m_targetHeight;
	DVLB_s* m_shaderBinary;
	shaderProgram_s m_program;
	std::map<std::string, int> m_uniforms;
	std::vector<std::string> m_report;

	Citro3D_Effect* loadEffect(const std::string& file, int flags);
	Citro3D_Effect* fallbackEffect();
	std::string resolve(const std::string& dir, const std::string& name) const;
};

END_NAMESPACE()
