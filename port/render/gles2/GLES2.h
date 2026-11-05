#pragma once
#include "EffectParser.h"
#include <gr/Context.h>
#include <gr/Shader.h>
#include <gr/Texture.h>
#include <gr/CubeTexture.h>
#include <gr/Palette.h>
#include <gr/Rect.h>
#include <gr/SurfaceFormat.h>
#include <gr/impl/DIPrimitive.h>
#include <gr/impl/SortBuffer.h>
#include <lang/String.h>
#include <math/float4x4.h>
#include <GLES2/gl2.h>
#include <map>
#include <set>
#include <string>
#include <vector>

BEGIN_NAMESPACE(gr)

class GLES2_Context;

class GLES2_Texture : public Texture {
public:
	GLES2_Texture(GLES2_Context* ctx, const lang::String& filename);
	GLES2_Texture(GLES2_Context* ctx, int width, int height);
	~GLES2_Texture();

	void blt(int x, int y, const void* data, int pitch, int w, int h, const SurfaceFormat& fmt, const void* pal, const SurfaceFormat& palfmt);
	void clear() {}
	int width() const { return m_width; }
	int height() const { return m_height; }
	Rect rect() const { return Rect(0, 0, m_width, m_height); }
	SurfaceFormat format() const { return SurfaceFormat(SurfaceFormat::SURFACE_A8R8G8B8); }
	void getData(void** bits, int* pitch) const { *bits = 0; *pitch = 0; }

	GLuint id() const { return m_id; }
	bool loaded() const { return m_loaded; }

private:
	GLuint m_id;
	int m_width, m_height;
	bool m_loaded;
	void createChecker();
};

class GLES2_CubeTexture : public CubeTexture {
public:
	int width() const { return 1; }
	int height() const { return 1; }
	SurfaceFormat format() const { return SurfaceFormat(SurfaceFormat::SURFACE_A8R8G8B8); }
};

class GLES2_Palette : public Palette {
public:
	explicit GLES2_Palette(int n) : m_n(n) {}
	void setEntries(int, const void*, const SurfaceFormat&, int) {}
	void getEntries(int, void*, const SurfaceFormat&, int) {}
	int entries() const { return m_n; }
private:
	int m_n;
};

struct GLES2_PassProgram {
	GLuint program;
	GLint attrib[VertexFormat::DT_SIZE];
	std::map<std::string, GLint> uniforms;
	std::map<std::string, GLenum> uniformTypes;
	std::map<std::string, int> uniformSizes;
	std::map<std::string, std::string> states;
};

class GLES2_Effect : public lang::Object {
public:
	std::string name;
	int priority;
	Shader::SortType sort;
	std::vector<std::string> techniqueNames;
	std::vector<std::vector<GLES2_PassProgram> > techniques;
	bool fallback;
	bool isWater() const {
		if (findTechnique("Water") >= 0) return true;
		std::string b = name.substr(name.find_last_of("/\\") == std::string::npos ? 0 : name.find_last_of("/\\") + 1);
		return b.compare(0, 5, "water") == 0;
	}
	int findTechnique(const std::string& n) const {
		for (size_t i = 0; i < techniqueNames.size(); ++i) if (techniqueNames[i] == n) return (int)i;
		return -1;
	}
};

class GLES2_Shader : public Shader {
public:
	GLES2_Shader(GLES2_Context* ctx, GLES2_Effect* fx);
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
	GLES2_Effect* effect() const { return m_fx; }

private:
	struct Value {
		enum Kind { VEC, MAT, MATARRAY, TEX } kind;
		float v[16];
		std::vector<float> arr;
		P(BaseTexture) tex;
	};

	GLES2_Context* m_ctx;
	P(GLES2_Effect) m_fx;
	int m_technique;
	int m_activeTechnique;
	SortType m_sort;
	lang::String m_name;
	std::map<std::string, Value> m_values;

	Value& value(const std::string& n, Value::Kind k);
	void applyStates(const GLES2_PassProgram& p);
	void applyUniforms(const GLES2_PassProgram& p);
};

class GLES2_Primitive : public DIPrimitive {
public:
	GLES2_Primitive(GLES2_Context* ctx, PrimType type, const VertexFormat& vf, int vertices, int indices);
	~GLES2_Primitive();
	void setShader(Shader* fx) { m_shader = fx; }
	Shader* shader() const { return m_shader; }
	PrimType type() const { return m_type; }
	void unlock();
	void render();

private:
	GLES2_Context* m_ctx;
	PrimType m_type;
	P(Shader) m_shader;
	GLuint m_vbo[VertexFormat::DT_SIZE];
	int m_vboComponents[VertexFormat::DT_SIZE];
	GLuint m_ibo;
	bool m_dirty;
	SortBuffer m_sortBuffer;
	void upload();
	void sortTriangles();
};

class GLES2_Context : public Context {
public:
	typedef void (*PresentFunc)(void* user);

	GLES2_Context(int width, int height, PresentFunc present, void* user);
	~GLES2_Context();

	void setShaderDir(const std::string& d) { m_shaderDir = d; }
	void setTextureDir(const std::string& d) { m_textureDir = d; }
	void setPlatformId(int id) { m_platformId = id; }
	void resize(int w, int h) { m_width = w; m_height = h; }

	Shader* createShader(const lang::String& name, int flags = 0);
	Palette* createPalette(int entries) { return new GLES2_Palette(entries); }
	Primitive* createPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices, UsageFlags usage = USAGE_STATIC);
	Primitive* getDynamicPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices);
	Texture* createTexture(int width, int height, const SurfaceFormat& fmt, Palette* pal, int usageflags);
	Texture* createTexture(const lang::String& filename);
	CubeTexture* createCubeTexture(const lang::String&) { return new GLES2_CubeTexture; }
	void setViewport(const Rect& rect);
	void setPerspectiveProjection(float hfov, float front, float back, float aspect);
	void setOrthographicProjection(bool enabled);
	void setRenderTarget(Texture* target);
	void beginScene() {}
	void endScene() {}
	void clear(int color = 0);
	void present(int color = 0);
	bool ready() { return true; }
	void capture(const lang::String& namefmt);
	int width() const { return m_width; }
	int height() const { return m_height; }
	SurfaceFormat surfaceFormat() const { return SurfaceFormat(SurfaceFormat::SURFACE_A8R8G8B8); }
	const Rect& viewport() const { return m_viewport; }
	const math::float4x4& projectionTransform() const { return m_proj; }
	PlatformType platform() const { return (PlatformType)m_platformId; }
	OrientationType orientation() const { return ORIENTATION_0; }

	const GLES2_PassProgram* currentPass;
	GLES2_Shader* currentShader;
	float time;
	math::float4 fogRange;
	math::float4 fogColor;
	int texturesLoaded, texturesFailed, effectsLoaded, effectsFallback;
	std::set<std::string> warnedSamplers;
	GLuint whiteTexture();
	GLuint grayTexture();
	GLuint neutralTexture(const std::string& sampler);
	bool neutralLightmap;

	std::string forcedTechnique;
	bool mirrored;
	bool obliqueClip;
	math::float4 clipPlane;
	P(Texture) reflectionTexture;
	P(Texture) waterNormal;

	bool saveScreenshot(const std::string& path);

private:
	int m_width, m_height, m_platformId;
	Rect m_viewport;
	math::float4x4 m_proj;
	PresentFunc m_present;
	void* m_presentUser;
	GLuint m_white, m_gray;
	struct Target { GLuint fbo, depth; int w, h; };
	std::map<GLuint, Target> m_targets;
	Texture* m_currentTarget;
	unsigned m_defaultFramebuffer = 0;
	int m_defaultFramebufferWidth = 0, m_defaultFramebufferHeight = 0;
public:
	void setDefaultFramebuffer(unsigned fbo, int width = 0, int height = 0) { m_defaultFramebuffer = fbo; m_defaultFramebufferWidth = width; m_defaultFramebufferHeight = height; }
	int renderWidth() const;
	int renderHeight() const;
private:
	std::map<std::string, GLuint> m_neutral;
	std::string m_shaderDir, m_textureDir;
	std::map<std::string, P(GLES2_Effect)> m_effects;
	P(GLES2_Effect) m_fallback;
	std::set<std::string> m_missingEffects;

	GLES2_Effect* loadEffect(const std::string& file, int flags);
	GLES2_Effect* fallbackEffect();
	std::string resolve(const std::string& dir, const std::string& name) const;
};

END_NAMESPACE()
