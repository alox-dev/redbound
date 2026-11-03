#pragma once
#include <gr/Context.h>
#include <gr/Shader.h>
#include <gr/Texture.h>
#include <gr/CubeTexture.h>
#include <gr/Palette.h>
#include <gr/Rect.h>
#include <gr/impl/DIPrimitive.h>
#include <math/float4x4.h>
#include <lang/String.h>
#include <map>
#include <string>

class NullShader : public gr::Shader {
public:
	explicit NullShader(const lang::String& n) : m_name(n) {}
	gr::Shader* clone() const { NullShader* c = new NullShader(m_name); c->textures = textures; return c; }
	void setTechnique(const char*) {}
	void setTexture(ParamType, gr::BaseTexture*) {}
	void setTexture(const char* n, gr::BaseTexture* t) { textures[n] = t; }
	std::map<std::string, P(gr::BaseTexture)> textures;
	void setMatrix(ParamType, const math::float4x4&) {}
	void setMatrix(const char*, const math::float4x4&) {}
	void setMatrixArray(ParamType, math::float4x4**, int) {}
	void setVector(ParamType, const math::float4&) {}
	void setVector(const char*, const math::float4&) {}
	void setFloat(ParamType, float) {}
	void setFloat(const char*, float) {}
	gr::BaseTexture* getTexture(const char*) { return 0; }
	math::float4x4 getMatrix(const char*) { return math::float4x4(1.f); }
	math::float4 getVector(const char*) { return math::float4(0,0,0,0); }
	float getFloat(const char*) { return 0.f; }
	int begin() { return 0; }
	void beginPass(int) {}
	void endPass() {}
	void end() {}
	void setName(const lang::String& n) { m_name = n; }
	void setSort(SortType) {}
	int priority() const { return 0; }
	SortType sort() const { return SORT_NONE; }
	bool enabled() const { return true; }
	const lang::String& name() const { return m_name; }
private:
	lang::String m_name;
};

class NullTexture : public gr::Texture {
public:
	std::string filename;
	void blt(int,int,const void*,int,int,int,const gr::SurfaceFormat&,const void*,const gr::SurfaceFormat&) {}
	void clear() {}
	int width() const { return 1; }
	int height() const { return 1; }
	gr::Rect rect() const { return gr::Rect(0,0,1,1); }
	gr::SurfaceFormat format() const { return gr::SurfaceFormat(); }
	void getData(void** b, int* p) const { *b = 0; *p = 0; }
};

class NullCubeTexture : public gr::CubeTexture {
public:
	int width() const { return 1; }
	int height() const { return 1; }
	gr::SurfaceFormat format() const { return gr::SurfaceFormat(); }
};

class NullPalette : public gr::Palette {
public:
	explicit NullPalette(int n) : m_n(n) {}
	void setEntries(int, const void*, const gr::SurfaceFormat&, int) {}
	void getEntries(int, void*, const gr::SurfaceFormat&, int) {}
	int entries() const { return m_n; }
private:
	int m_n;
};

class NullPrimitive : public gr::DIPrimitive {
public:
	NullPrimitive(PrimType t, const gr::VertexFormat& vf, int v, int i) : m_type(t), m_shader(0) { setFormat(vf, v, i); }
	void setShader(gr::Shader* s) { m_shader = s; }
	gr::Shader* shader() const { return m_shader.ptr(); }
	PrimType type() const { return m_type; }
	void render() {}
private:
	PrimType m_type;
	P(gr::Shader) m_shader;
};

class NullContext : public gr::Context {
public:
	explicit NullContext(PlatformType p = PLATFORM_EGL) : m_vp(0,0,800,480), m_proj(1.f), m_platform(p) {}
	gr::Shader* createShader(const lang::String& name, int) { return new NullShader(name); }
	gr::Palette* createPalette(int n) { return new NullPalette(n); }
	gr::Primitive* createPrimitive(gr::Primitive::PrimType p, const gr::VertexFormat& vf, int v, int i, UsageFlags) { return new NullPrimitive(p, vf, v, i); }
	gr::Primitive* getDynamicPrimitive(gr::Primitive::PrimType p, const gr::VertexFormat& vf, int v, int i) { return new NullPrimitive(p, vf, v, i); }
	gr::Texture* createTexture(int, int, const gr::SurfaceFormat&, gr::Palette*, int) { return new NullTexture; }
	gr::Texture* createTexture(const lang::String& f) { NullTexture* t = new NullTexture; t->filename = f.c_str(); return t; }
	gr::CubeTexture* createCubeTexture(const lang::String&) { return new NullCubeTexture; }
	void setViewport(const gr::Rect& r) { m_vp = r; }
	void setPerspectiveProjection(float, float, float, float) {}
	void setOrthographicProjection(bool) {}
	void setRenderTarget(gr::Texture*) {}
	void beginScene() {}
	void endScene() {}
	void clear(int) {}
	void present(int) {}
	bool ready() { return true; }
	void capture(const lang::String&) {}
	int width() const { return 800; }
	int height() const { return 480; }
	gr::SurfaceFormat surfaceFormat() const { return gr::SurfaceFormat(); }
	const gr::Rect& viewport() const { return m_vp; }
	const math::float4x4& projectionTransform() const { return m_proj; }
	PlatformType platform() const { return m_platform; }
	OrientationType orientation() const { return ORIENTATION_0; }
private:
	gr::Rect m_vp;
	math::float4x4 m_proj;
	PlatformType m_platform;
};
