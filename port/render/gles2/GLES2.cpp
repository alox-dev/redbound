#include "GLES2.h"
#include "ImageLoader.h"
#include <lang/Debug.h>
#include <math/float4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <cmath>
#include <algorithm>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

USING_NAMESPACE(lang)
USING_NAMESPACE(math)

BEGIN_NAMESPACE(gr)

static bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

static std::string baseName(const std::string& p) {
	size_t i = p.find_last_of("/\\");
	return i == std::string::npos ? p : p.substr(i + 1);
}

static void checkGL(const char* where) {
	GLenum e = glGetError();
	if (e != GL_NO_ERROR) fprintf(stderr, "[gles2] error GL 0x%04x en %s\n", e, where);
}

static bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

GLES2_Texture::GLES2_Texture(GLES2_Context* ctx, const String& filename) :
	m_id(0), m_width(1), m_height(1), m_loaded(false)
{
	redbound::Image img;
	std::string path = filename.c_str();
	if (!redbound::loadImage(path, img)) {
		fprintf(stderr, "[gles2] texture '%s': %s\n", path.c_str(), img.error.c_str());
		createChecker();
		++ctx->texturesFailed;
		return;
	}
	m_width = img.levels[0].width;
	m_height = img.levels[0].height;
	glGenTextures(1, &m_id);
	glBindTexture(GL_TEXTURE_2D, m_id);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	const bool pot = isPow2(m_width) && isPow2(m_height);
	const redbound::ImageLevel& last = img.levels.back();
	const bool fullChain = img.levels.size() > 1 && last.width == 1 && last.height == 1;
	const size_t uploadLevels = pot ? img.levels.size() : 1;
	for (size_t l = 0; l < uploadLevels; ++l)
		glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, img.levels[l].width, img.levels[l].height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &img.levels[l].rgba[0]);
	if (pot) {
		if (!fullChain) glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	checkGL("GLES2_Texture");
	m_loaded = true;
	++ctx->texturesLoaded;
}

GLES2_Texture::GLES2_Texture(GLES2_Context*, int width, int height) :
	m_id(0), m_width(width), m_height(height), m_loaded(true)
{
	glGenTextures(1, &m_id);
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void GLES2_Texture::createChecker() {
	m_width = m_height = 8;
	uint8_t px[8 * 8 * 4];
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x) {
			uint8_t* p = px + (y * 8 + x) * 4;
			bool c = ((x / 2) + (y / 2)) & 1;
			p[0] = c ? 255 : 90; p[1] = c ? 0 : 90; p[2] = c ? 255 : 90; p[3] = 255;
		}
	glGenTextures(1, &m_id);
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

GLES2_Texture::~GLES2_Texture() {
	if (m_id) glDeleteTextures(1, &m_id);
}

void GLES2_Texture::blt(int x, int y, const void* data, int pitch, int w, int h, const SurfaceFormat& fmt, const void*, const SurfaceFormat&) {
	if (fmt.type() != SurfaceFormat::SURFACE_A8R8G8B8 || !data) return;
	std::vector<uint8_t> rgba((size_t)w * h * 4);
	for (int j = 0; j < h; ++j) {
		const uint8_t* s = (const uint8_t*)data + (size_t)j * pitch;
		for (int i = 0; i < w; ++i) {
			uint8_t* d = &rgba[((size_t)j * w + i) * 4];
			d[0] = s[i * 4 + 2]; d[1] = s[i * 4 + 1]; d[2] = s[i * 4 + 0]; d[3] = s[i * 4 + 3];
		}
	}
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
}

static GLuint compileStage(GLenum type, const std::string& src, const std::string& label) {
	GLuint s = glCreateShader(type);
	const char* p = src.c_str();
	glShaderSource(s, 1, &p, 0);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(s, sizeof(log), 0, log);
		fprintf(stderr, "[gles2] error compilando %s:\n%s\n", label.c_str(), log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static const char* const ATTRIB_NAMES[VertexFormat::DT_SIZE] = {
	"DT_POSITION", "DT_POSITIONT", "DT_BONEWEIGHTS", "DT_BONEINDICES", "DT_NORMAL", "DT_DIFFUSE",
	"DT_SPECULAR", "DT_TEX0", "DT_TEX1", "DT_TEX2", "DT_TEX3", "DT_TANGENT"
};

static bool linkProgram(const std::string& vs, const std::string& fs, const std::string& label, GLES2_PassProgram& out) {
	GLuint v = compileStage(GL_VERTEX_SHADER, vs, label + " (vertex)");
	GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, label + " (pixel)");
	if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return false; }
	GLuint p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glBindAttribLocation(p, 0, "DT_POSITION");
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetProgramInfoLog(p, sizeof(log), 0, log);
		fprintf(stderr, "[gles2] error enlazando %s:\n%s\n", label.c_str(), log);
		glDeleteProgram(p);
		return false;
	}
	out.program = p;
	for (int i = 0; i < VertexFormat::DT_SIZE; ++i)
		out.attrib[i] = glGetAttribLocation(p, ATTRIB_NAMES[i]);
	GLint count = 0;
	glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &count);
	for (GLint i = 0; i < count; ++i) {
		char name[256];
		GLint size;
		GLenum type;
		glGetActiveUniform(p, (GLuint)i, sizeof(name), 0, &size, &type, name);
		std::string n = name;
		size_t br = n.find('[');
		if (br != std::string::npos) n = n.substr(0, br);
		out.uniforms[n] = glGetUniformLocation(p, n.c_str());
		out.uniformTypes[n] = type;
		out.uniformSizes[n] = size;
	}
	return true;
}

static const char* FALLBACK_VS =
	"#version 100\n"
	"attribute highp vec3 DT_POSITION;\n"
	"attribute mediump vec3 DT_NORMAL;\n"
	"attribute mediump vec2 DT_TEX0;\n"
	"uniform highp mat4 TOTALTM;\n"
	"uniform highp mat4 WORLDTM;\n"
	"uniform highp vec4 LIGHTP0;\n"
	"varying mediump vec2 V_TexCoord0;\n"
	"varying lowp float V_Light;\n"
	"void main() {\n"
	"  gl_Position = TOTALTM * vec4(DT_POSITION, 1.0);\n"
	"  highp vec3 wp = (WORLDTM * vec4(DT_POSITION, 1.0)).xyz;\n"
	"  mediump vec3 n = mat3(WORLDTM) * DT_NORMAL;\n"
	"  mediump float len = length(n);\n"
	"  n = len > 0.0001 ? n / len : vec3(0.0, 1.0, 0.0);\n"
	"  V_Light = 0.45 + 0.55 * abs(dot(n, normalize(LIGHTP0.xyz - wp + vec3(0.0, 0.0001, 0.0))));\n"
	"  V_TexCoord0 = DT_TEX0;\n"
	"}\n";

static const char* FALLBACK_FS =
	"#version 100\n"
	"precision mediump float;\n"
	"uniform lowp sampler2D BASEMAP;\n"
	"uniform lowp vec4 DIFFUSEC;\n"
	"varying mediump vec2 V_TexCoord0;\n"
	"varying lowp float V_Light;\n"
	"void main() {\n"
	"  lowp vec4 c = texture2D(BASEMAP, V_TexCoord0);\n"
	"  gl_FragColor = vec4(c.rgb * V_Light, c.a);\n"
	"}\n";

static bool invert4(const float4x4& m, float4x4& out);

GLES2_Context::GLES2_Context(int width, int height, PresentFunc present, void* user) :
	currentPass(0), currentShader(0), time(0.f), fogRange(1e6f, 2e6f, 1e-6f, 0.f), fogColor(0.5f, 0.6f, 0.7f, 1.f),
	texturesLoaded(0), texturesFailed(0), effectsLoaded(0), effectsFallback(0),
	m_width(width), m_height(height), m_platformId(5), m_viewport(0, 0, width, height), m_proj(1.f),
	m_present(present), m_presentUser(user), m_white(0), m_gray(0), m_currentTarget(0)
{
	neutralLightmap = false;
	mirrored = false;
	obliqueClip = false;
	glFrontFace(GL_CW);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glViewport(0, 0, width, height);
}

GLES2_Context::~GLES2_Context() {
	if (m_white) glDeleteTextures(1, &m_white);
	if (m_gray) glDeleteTextures(1, &m_gray);
	for (std::map<GLuint, Target>::iterator i = m_targets.begin(); i != m_targets.end(); ++i) {
		glDeleteFramebuffers(1, &i->second.fbo);
		glDeleteRenderbuffers(1, &i->second.depth);
	}
	for (std::map<std::string, GLuint>::iterator i = m_neutral.begin(); i != m_neutral.end(); ++i) glDeleteTextures(1, &i->second);
}

GLuint GLES2_Context::neutralTexture(const std::string& sampler) {
	uint8_t c[4] = { 255, 255, 255, 255 };
	if (sampler == "REFLMAP" || sampler == "REFRACMAP") { c[0] = c[1] = c[2] = 0; }
	else if (sampler == "NORMALMAP") { c[0] = 128; c[1] = 128; c[2] = 255; }
	else if (sampler == "SIMAP") { c[0] = c[1] = c[2] = 128; }
	else return whiteTexture();
	GLuint& id = m_neutral[sampler];
	if (!id) {
		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, c);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	return id;
}

GLuint GLES2_Context::grayTexture() {
	if (!m_gray) {
		const uint8_t px[4] = { 128, 128, 128, 255 };
		glGenTextures(1, &m_gray);
		glBindTexture(GL_TEXTURE_2D, m_gray);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	return m_gray;
}

GLuint GLES2_Context::whiteTexture() {
	if (!m_white) {
		const uint8_t px[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &m_white);
		glBindTexture(GL_TEXTURE_2D, m_white);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	return m_white;
}

std::string GLES2_Context::resolve(const std::string& dir, const std::string& name) const {
	std::string n = name;
	std::replace(n.begin(), n.end(), '\\', '/');
	if (fileExists(n)) return n;
	std::string b = baseName(n);
	if (!dir.empty() && fileExists(redbound::joinPath(dir, b))) return redbound::joinPath(dir, b);
	return std::string();
}

GLES2_Effect* GLES2_Context::fallbackEffect() {
	if (!m_fallback) {
		m_fallback = new GLES2_Effect;
		m_fallback->name = "(fallback)";
		m_fallback->priority = 0;
		m_fallback->sort = Shader::SORT_NONE;
		m_fallback->fallback = true;
		GLES2_PassProgram p;
		if (!linkProgram(FALLBACK_VS, FALLBACK_FS, "fallback", p)) {
			fprintf(stderr, "[gles2] the fallback shader does not compile; check the GLES context\n");
			abort();
		}
		m_fallback->techniqueNames.push_back("Default");
		m_fallback->techniques.push_back(std::vector<GLES2_PassProgram>(1, p));
	}
	return m_fallback;
}

GLES2_Effect* GLES2_Context::loadEffect(const std::string& file, int flags) {
	char key[32];
	snprintf(key, sizeof(key), "#%d", flags);
	std::string k = file + key;
	std::map<std::string, P(GLES2_Effect)>::iterator it = m_effects.find(k);
	if (it != m_effects.end()) return it->second;

	redbound::Effect fx;
	std::vector<std::string> extra;
	if (!redbound::parseEffect(file, extra, fx)) {
		fprintf(stderr, "[gles2] effect '%s': %s (using fallback shader)\n", file.c_str(), fx.error.c_str());
		++effectsFallback;
		return m_effects[k] = fallbackEffect();
	}
	P(GLES2_Effect) e = new GLES2_Effect;
	e->name = file;
	e->fallback = false;
	e->priority = fx.globals.count("priority") ? atoi(fx.globals["priority"].c_str()) : 0;
	std::string sort = fx.globals.count("sort") ? fx.globals["sort"] : "";
	std::transform(sort.begin(), sort.end(), sort.begin(), ::toupper);
	e->sort = sort == "BACKTOFRONT" ? Shader::SORT_BACKTOFRONT : sort == "FRONTTOBACK" ? Shader::SORT_FRONTTOBACK :
	          sort == "INSIDEOUT" ? Shader::SORT_INSIDEOUT : sort == "OUTSIDEIN" ? Shader::SORT_OUTSIDEIN : Shader::SORT_NONE;
	for (size_t t = 0; t < fx.techniques.size(); ++t) {
		std::vector<GLES2_PassProgram> passes;
		for (size_t p = 0; p < fx.techniques[t].passes.size(); ++p) {
			const redbound::EffectPass& ep = fx.techniques[t].passes[p];
			std::string vs, fs, err;
			GLES2_PassProgram prog;
			std::string label = baseName(file) + "/" + fx.techniques[t].name + "/" + ep.name;
			if (!redbound::buildShaderSource(ep.vertexShaderPath, fx.defines, false, vs, err) ||
			    !redbound::buildShaderSource(ep.pixelShaderPath, fx.defines, true, fs, err) ||
			    !linkProgram(vs, fs, label, prog)) {
				if (!err.empty()) fprintf(stderr, "[gles2] %s: %s\n", label.c_str(), err.c_str());
				fprintf(stderr, "[gles2] effect '%s' failed; using fallback shader\n", file.c_str());
				++effectsFallback;
				return m_effects[k] = fallbackEffect();
			}
			prog.states = ep.states;
			passes.push_back(prog);
		}
		e->techniqueNames.push_back(fx.techniques[t].name);
		e->techniques.push_back(passes);
	}
	++effectsLoaded;
	m_effects[k] = e;
	return e;
}

Shader* GLES2_Context::createShader(const String& name, int flags) {
	std::string file = resolve(m_shaderDir, name.c_str());
	if (file.empty()) file = resolve(m_shaderDir, std::string(name.c_str()) + ".fx");
	GLES2_Effect* e;
	if (file.empty()) {
		if (m_missingEffects.insert(name.c_str()).second)
			fprintf(stderr, "[gles2] effect '%s' not found (using fallback shader)\n", name.c_str());
		++effectsFallback;
		e = fallbackEffect();
	} else {
		e = loadEffect(file, flags);
	}
	return new GLES2_Shader(this, e);
}

Primitive* GLES2_Context::createPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices, UsageFlags) {
	return new GLES2_Primitive(this, prim, vf, vertices, indices);
}

Primitive* GLES2_Context::getDynamicPrimitive(Primitive::PrimType prim, const VertexFormat& vf, int vertices, int indices) {
	return new GLES2_Primitive(this, prim, vf, vertices, indices);
}

Texture* GLES2_Context::createTexture(int width, int height, const SurfaceFormat&, Palette*, int) {
	return new GLES2_Texture(this, width, height);
}

Texture* GLES2_Context::createTexture(const String& filename) {
	std::string f = resolve(m_textureDir, filename.c_str());
	if (f.empty()) {
		std::string n = baseName(filename.c_str());
		size_t dot = n.find_last_of('.');
		if (dot != std::string::npos) f = resolve(m_textureDir, n.substr(0, dot) + ".pvr");
	}
	if (f.empty()) f = filename.c_str();
	return new GLES2_Texture(this, f.c_str());
}

int GLES2_Context::renderWidth() const {
	if (m_currentTarget) return m_currentTarget->width();
	return m_defaultFramebuffer && m_defaultFramebufferWidth > 0 ? m_defaultFramebufferWidth : m_width;
}

int GLES2_Context::renderHeight() const {
	if (m_currentTarget) return m_currentTarget->height();
	return m_defaultFramebuffer && m_defaultFramebufferHeight > 0 ? m_defaultFramebufferHeight : m_height;
}

void GLES2_Context::setViewport(const Rect& r) {
	m_viewport = r;
	glViewport(r.left(), renderHeight() - r.top() - r.height(), r.width(), r.height());
}

void GLES2_Context::setPerspectiveProjection(float hfov, float front, float back, float aspect) {
	m_proj.setPerspectiveProjection(hfov, front, back, aspect);
	for (int c = 0; c < 4; ++c)
		m_proj(2, c) = 2.f * m_proj(2, c) - m_proj(3, c);

	if (obliqueClip) {
		float4x4 inv;
		if (invert4(m_proj, inv)) {
			const float cx = clipPlane.x > 0.f ? 1.f : clipPlane.x < 0.f ? -1.f : 0.f;
			const float cy = clipPlane.y > 0.f ? 1.f : clipPlane.y < 0.f ? -1.f : 0.f;
			float q[4];
			for (int r = 0; r < 4; ++r) q[r] = inv(r, 0) * cx + inv(r, 1) * cy + inv(r, 2) + inv(r, 3);
			const float c[4] = { clipPlane.x, clipPlane.y, clipPlane.z, clipPlane.w };
			const float d = c[0] * q[0] + c[1] * q[1] + c[2] * q[2] + c[3] * q[3];
			if (fabsf(d) > 1e-12f) {
				const float k = 2.f / d;
				for (int col = 0; col < 4; ++col) m_proj(2, col) = c[col] * k - m_proj(3, col);
			}
		}
	}
}

void GLES2_Context::setRenderTarget(Texture* target) {
	GLES2_Texture* t = dynamic_cast<GLES2_Texture*>(target);
	if (!t) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_defaultFramebuffer);
		m_currentTarget = 0;
		return;
	}
	Target& rt = m_targets[t->id()];
	if (!rt.fbo) {
		glGenFramebuffers(1, &rt.fbo);
		glGenRenderbuffers(1, &rt.depth);
		glBindRenderbuffer(GL_RENDERBUFFER, rt.depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, t->width(), t->height());
		glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id(), 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.depth);
		const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (st != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[gles2] framebuffer incomplete (0x%04x)\n", st);
		rt.w = t->width();
		rt.h = t->height();
	}
	glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
	m_currentTarget = t;
}

static bool invert4(const float4x4& m, float4x4& out) {
	float a[16], inv[16];
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) a[r * 4 + c] = m(r, c);
	inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
	inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
	inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
	inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
	inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
	inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
	inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
	inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
	inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
	inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
	inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
	inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
	inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
	inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
	inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
	inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
	const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
	if (fabsf(det) < 1e-12f) return false;
	for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out(r, c) = inv[r * 4 + c] / det;
	return true;
}

void GLES2_Context::setOrthographicProjection(bool enabled) {
	if (enabled) m_proj = float4x4(1.f);
}

void GLES2_Context::clear(int color) {
	glClearColor(((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f, (color & 255) / 255.f, ((color >> 24) & 255) / 255.f);
	glDepthMask(GL_TRUE);
	glClearDepthf(1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLES2_Context::present(int) {
	if (m_present) m_present(m_presentUser);
}

bool GLES2_Context::saveScreenshot(const std::string& path) {
	std::vector<uint8_t> px((size_t)m_width * m_height * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
	std::vector<uint8_t> flipped(px.size());
	const size_t row = (size_t)m_width * 4;
	for (int y = 0; y < m_height; ++y)
		memcpy(&flipped[y * row], &px[(m_height - 1 - y) * row], row);
	for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
	return stbi_write_png(path.c_str(), m_width, m_height, 4, &flipped[0], (int)row) != 0;
}

void GLES2_Context::capture(const String& namefmt) {
	saveScreenshot(namefmt.c_str());
}

const char* GLES2_Shader::paramName(ParamType p) {
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

GLES2_Shader::GLES2_Shader(GLES2_Context* ctx, GLES2_Effect* fx) :
	m_ctx(ctx), m_fx(fx), m_technique(0), m_activeTechnique(0), m_sort(fx->sort)
{
}

Shader* GLES2_Shader::clone() const {
	GLES2_Shader* s = new GLES2_Shader(m_ctx, m_fx);
	s->m_technique = m_technique;
	s->m_sort = m_sort;
	s->m_name = m_name;
	s->m_values = m_values;
	return s;
}

void GLES2_Shader::setTechnique(const char* name) {
	for (size_t i = 0; i < m_fx->techniqueNames.size(); ++i)
		if (m_fx->techniqueNames[i] == name) { m_technique = (int)i; return; }
}

GLES2_Shader::Value& GLES2_Shader::value(const std::string& n, Value::Kind k) {
	Value& v = m_values[n];
	v.kind = k;
	return v;
}

static void toColumnMajor(const float4x4& m, float* out) {
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			out[c * 4 + r] = m(r, c);
}

void GLES2_Shader::setTexture(ParamType p, BaseTexture* t) { setTexture(paramName(p), t); }
void GLES2_Shader::setTexture(const char* name, BaseTexture* t) { value(name, Value::TEX).tex = t; }
void GLES2_Shader::setMatrix(ParamType p, const float4x4& m) { setMatrix(paramName(p), m); }
void GLES2_Shader::setMatrix(const char* name, const float4x4& m) { toColumnMajor(m, value(name, Value::MAT).v); }
void GLES2_Shader::setMatrixArray(ParamType p, float4x4** ms, int count) {
	Value& v = value(paramName(p), Value::MATARRAY);
	v.arr.resize((size_t)count * 16);
	for (int i = 0; i < count; ++i) toColumnMajor(*ms[i], &v.arr[(size_t)i * 16]);
}
void GLES2_Shader::setVector(ParamType p, const float4& x) { setVector(paramName(p), x); }
void GLES2_Shader::setVector(const char* name, const float4& x) {
	Value& v = value(name, Value::VEC);
	v.v[0] = x.x; v.v[1] = x.y; v.v[2] = x.z; v.v[3] = x.w;
}
void GLES2_Shader::setFloat(ParamType p, float f) { setFloat(paramName(p), f); }
void GLES2_Shader::setFloat(const char* name, float f) {
	Value& v = value(name, Value::VEC);
	v.v[0] = v.v[1] = v.v[2] = v.v[3] = f;
}

BaseTexture* GLES2_Shader::getTexture(const char* name) {
	std::map<std::string, Value>::iterator it = m_values.find(name);
	return it != m_values.end() && it->second.kind == Value::TEX ? (BaseTexture*)it->second.tex : 0;
}
float4x4 GLES2_Shader::getMatrix(const char* name) {
	std::map<std::string, Value>::iterator it = m_values.find(name);
	float4x4 m(1.f);
	if (it != m_values.end() && it->second.kind == Value::MAT)
		for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) m(r, c) = it->second.v[c * 4 + r];
	return m;
}
float4 GLES2_Shader::getVector(const char* name) {
	std::map<std::string, Value>::iterator it = m_values.find(name);
	if (it == m_values.end() || it->second.kind != Value::VEC) return float4(0, 0, 0, 0);
	return float4(it->second.v[0], it->second.v[1], it->second.v[2], it->second.v[3]);
}
float GLES2_Shader::getFloat(const char* name) { return getVector(name).x; }

int GLES2_Shader::begin() {
	m_activeTechnique = m_technique;
	if (!m_ctx->forcedTechnique.empty()) {
		m_activeTechnique = m_fx->findTechnique(m_ctx->forcedTechnique);
		if (m_fx->fallback) m_activeTechnique = 0;
		if (m_activeTechnique < 0) return 0;
	}
	return (int)m_fx->techniques[m_activeTechnique].size();
}

static GLenum blendFactor(const std::string& s, GLenum def) {
	if (s == "ZERO") return GL_ZERO;
	if (s == "ONE") return GL_ONE;
	if (s == "SRC_COLOR") return GL_SRC_COLOR;
	if (s == "ONE_MINUS_SRC_COLOR") return GL_ONE_MINUS_SRC_COLOR;
	if (s == "DST_COLOR") return GL_DST_COLOR;
	if (s == "ONE_MINUS_DST_COLOR") return GL_ONE_MINUS_DST_COLOR;
	if (s == "SRC_ALPHA") return GL_SRC_ALPHA;
	if (s == "ONE_MINUS_SRC_ALPHA") return GL_ONE_MINUS_SRC_ALPHA;
	if (s == "DST_ALPHA") return GL_DST_ALPHA;
	if (s == "ONE_MINUS_DST_ALPHA") return GL_ONE_MINUS_DST_ALPHA;
	if (s == "SRC_ALPHA_SATURATE") return GL_SRC_ALPHA_SATURATE;
	return def;
}

static std::string upper(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::toupper); return s; }

void GLES2_Shader::applyStates(const GLES2_PassProgram& p) {
	glFrontFace(m_ctx->mirrored ? GL_CCW : GL_CW);
	std::map<std::string, std::string> st;
	for (std::map<std::string, std::string>::const_iterator i = p.states.begin(); i != p.states.end(); ++i) st[i->first] = upper(i->second);
	static const auto isSky = [](const std::string& n) { std::string l = n; std::transform(l.begin(), l.end(), l.begin(), ::tolower); return l.find("skybox") != std::string::npos; };
	const bool cull = (!st.count("enableculling") || st["enableculling"] != "FALSE") && !isSky(std::string(m_name.c_str()));
	if (cull) {
		glEnable(GL_CULL_FACE);
		const std::string f = st.count("cullface") ? st["cullface"] : "BACK";
		glCullFace(f == "FRONT" ? GL_FRONT : f == "FRONT_AND_BACK" ? GL_FRONT_AND_BACK : GL_BACK);
	} else {
		glDisable(GL_CULL_FACE);
	}
	const bool noDepthTest = st.count("enabledepthtest") && st["enabledepthtest"] == "FALSE";
	if (noDepthTest) {
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-4.f, -64.f);
	} else {
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	glDepthMask(noDepthTest || (st.count("enabledepthmask") && st["enabledepthmask"] == "FALSE") ? GL_FALSE : GL_TRUE);
	const std::string df = st.count("depthfunc") ? st["depthfunc"] : "LESS";
	glDepthFunc(df == "NEVER" ? GL_NEVER : df == "ALWAYS" ? GL_ALWAYS : df == "LEQUAL" ? GL_LEQUAL : df == "EQUAL" ? GL_EQUAL :
	            df == "GREATER" ? GL_GREATER : df == "GEQUAL" ? GL_GEQUAL : df == "NOTEQUAL" ? GL_NOTEQUAL : GL_LESS);
	if (st.count("enableblending") && st["enableblending"] == "TRUE") {
		glEnable(GL_BLEND);
		const std::string eq = st.count("blendequation") ? st["blendequation"] : "FUNC_ADD";
		glBlendEquation(eq == "FUNC_SUBTRACT" ? GL_FUNC_SUBTRACT : eq == "FUNC_REVERSE_SUBTRACT" ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
		glBlendFunc(blendFactor(st["blendfuncsrc"], GL_ONE), blendFactor(st["blendfuncdst"], GL_ZERO));
	} else {
		glDisable(GL_BLEND);
	}
}

void GLES2_Shader::applyUniforms(const GLES2_PassProgram& p) {
	int unit = 0;
	for (std::map<std::string, GLint>::const_iterator it = p.uniforms.begin(); it != p.uniforms.end(); ++it) {
		const std::string& n = it->first;
		const GLint loc = it->second;
		const GLenum type = p.uniformTypes.find(n)->second;
		if (loc < 0) continue;

		if (type == GL_SAMPLER_2D || type == GL_SAMPLER_CUBE) {
			std::map<std::string, Value>::iterator v = m_values.find(n);
			GLES2_Texture* t = 0;
			if (v != m_values.end() && v->second.kind == Value::TEX && v->second.tex)
				t = dynamic_cast<GLES2_Texture*>((BaseTexture*)v->second.tex);
			if (m_ctx->neutralLightmap && n == "SIMAP") {
				glActiveTexture(GL_TEXTURE0 + unit);
				glBindTexture(GL_TEXTURE_2D, m_ctx->grayTexture());
				glUniform1i(loc, unit);
				++unit;
				continue;
			}
			if (!t && m_fx->isWater()) {
				if (n == "REFLMAP" && m_ctx->reflectionTexture) t = dynamic_cast<GLES2_Texture*>((Texture*)m_ctx->reflectionTexture);
				else if (n == "NORMALMAP" && m_ctx->waterNormal) t = dynamic_cast<GLES2_Texture*>((Texture*)m_ctx->waterNormal);
			}
			if (!t) {
				std::string key = m_fx->name + "|" + n;
				if (m_ctx->warnedSamplers.insert(key).second) {
					std::string have;
					for (std::map<std::string, Value>::const_iterator i = m_values.begin(); i != m_values.end(); ++i)
						if (i->second.kind == Value::TEX) have += (have.empty() ? "" : ", ") + i->first;
					fprintf(stderr, "[gles2] sampler '%s' has no texture in '%s' (material '%s' provides: %s)\n",
					        n.c_str(), baseName(m_fx->name).c_str(), m_name.c_str(), have.empty() ? "nothing" : have.c_str());
				}
			}
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, t ? t->id() : m_ctx->neutralTexture(n));
			glUniform1i(loc, unit);
			++unit;
			continue;
		}

		float tmp[16];
		const float* data = 0;
		std::map<std::string, Value>::iterator v = m_values.find(n);
		if (v != m_values.end()) {
			if (v->second.kind == Value::MATARRAY) {
				if (type == GL_FLOAT_MAT4 && !v->second.arr.empty()) {
					int cnt = std::min((int)(v->second.arr.size() / 16), p.uniformSizes.find(n)->second);
					glUniformMatrix4fv(loc, cnt, GL_FALSE, &v->second.arr[0]);
				}
				continue;
			}
			data = v->second.v;
		} else if (n == "TIME") {
			tmp[0] = tmp[1] = tmp[2] = tmp[3] = m_ctx->time; data = tmp;
		} else if (n == "FOG_RANGE") {
			tmp[0] = m_ctx->fogRange.x; tmp[1] = m_ctx->fogRange.y; tmp[2] = m_ctx->fogRange.z; tmp[3] = m_ctx->fogRange.w; data = tmp;
		} else if (n == "FOG_COLOR") {
			tmp[0] = m_ctx->fogColor.x; tmp[1] = m_ctx->fogColor.y; tmp[2] = m_ctx->fogColor.z; tmp[3] = m_ctx->fogColor.w; data = tmp;
		} else if (n == "TEXEL_OFFSET") {
			tmp[0] = 1.f / m_ctx->renderWidth(); tmp[1] = 1.f / m_ctx->renderHeight(); tmp[2] = tmp[3] = 0.f; data = tmp;
		} else {
			float d = 0.f;
			if (n == "ALPHA_FACTOR" || n == "DIFFUSEC" || n == "LIGHTC0") d = 1.f;
			else if (n == "AMBIENTC") d = 0.35f;
			else if (n == "GLOSSINESS") d = 16.f;
			else if (n == "SHININESS") d = 0.3f;
			tmp[0] = tmp[1] = tmp[2] = tmp[3] = d;
			if (n == "DIFFUSEC" || n == "AMBIENTC" || n == "LIGHTC0") tmp[3] = 1.f;
			data = tmp;
		}
		switch (type) {
		case GL_FLOAT: glUniform1fv(loc, 1, data); break;
		case GL_FLOAT_VEC2: glUniform2fv(loc, 1, data); break;
		case GL_FLOAT_VEC3: glUniform3fv(loc, 1, data); break;
		case GL_FLOAT_VEC4: glUniform4fv(loc, 1, data); break;
		case GL_FLOAT_MAT4:
			if (v != m_values.end() && v->second.kind == Value::MAT) glUniformMatrix4fv(loc, 1, GL_FALSE, data);
			else { float id[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }; glUniformMatrix4fv(loc, 1, GL_FALSE, id); }
			break;
		default: break;
		}
	}
	glActiveTexture(GL_TEXTURE0);
}

void GLES2_Shader::beginPass(int pass) {
	const GLES2_PassProgram& p = m_fx->techniques[m_activeTechnique][pass];
	glUseProgram(p.program);
	applyStates(p);
	applyUniforms(p);
	m_ctx->currentPass = &p;
	m_ctx->currentShader = this;
}

void GLES2_Shader::endPass() { m_ctx->currentPass = 0; m_ctx->currentShader = 0; }
void GLES2_Shader::end() {}

GLES2_Primitive::GLES2_Primitive(GLES2_Context* ctx, PrimType type, const VertexFormat& vf, int vertices, int indices) :
	m_ctx(ctx), m_type(type), m_ibo(0), m_dirty(true)
{
	memset(m_vbo, 0, sizeof(m_vbo));
	memset(m_vboComponents, 0, sizeof(m_vboComponents));
	setFormat(vf, vertices, indices);
}

GLES2_Primitive::~GLES2_Primitive() {
	for (int i = 0; i < VertexFormat::DT_SIZE; ++i) if (m_vbo[i]) glDeleteBuffers(1, &m_vbo[i]);
	if (m_ibo) glDeleteBuffers(1, &m_ibo);
}

void GLES2_Primitive::unlock() {
	if (locked() & LOCK_WRITE) m_dirty = true;
	DIPrimitive::unlock();
}

void GLES2_Primitive::upload() {
	const int n = vertices();
	if (n <= 0) return;
	lock(LOCK_READ);
	std::vector<float4> tmp((size_t)n);
	std::vector<float> buf;
	const float4 psb = vertexPositionScaleBias();
	const float4 tsb = vertexTextureCoordinateScaleBias();
	for (int dt = 0; dt < VertexFormat::DT_SIZE; ++dt) {
		if (!vertexFormat().hasData((VertexFormat::DataType)dt)) continue;
		int comps = 4;
		switch (dt) {
		case VertexFormat::DT_POSITION: getVertexPositions(0, &tmp[0], n); comps = 3; break;
		case VertexFormat::DT_POSITIONT: getVertexData(VertexFormat::DT_POSITIONT, 0, &tmp[0], n); break;
		case VertexFormat::DT_NORMAL: getVertexNormals(0, &tmp[0], n); comps = 3; break;
		case VertexFormat::DT_TANGENT: getVertexTangents(0, &tmp[0], n); comps = 3; break;
		case VertexFormat::DT_DIFFUSE: getVertexDiffuseColors(0, &tmp[0], n); break;
		case VertexFormat::DT_SPECULAR: getVertexSpecularColors(0, &tmp[0], n); break;
		case VertexFormat::DT_BONEWEIGHTS: getVertexBoneWeights(0, &tmp[0], n); break;
		case VertexFormat::DT_BONEINDICES: getVertexBoneIndices(0, &tmp[0], n); break;
		default: getVertexTextureCoordinates(0, dt - VertexFormat::DT_TEX0, &tmp[0], n); comps = 2; break;
		}
		buf.resize((size_t)n * comps);
		for (int i = 0; i < n; ++i) {
			float4 v = tmp[i];
			if (dt == VertexFormat::DT_POSITION) {
				v.x = v.x * psb.x + psb.y; v.y = v.y * psb.x + psb.z; v.z = v.z * psb.x + psb.w;
			} else if (dt >= VertexFormat::DT_TEX0 && dt <= VertexFormat::DT_TEX3) {
				v.x = v.x * tsb.x + tsb.y; v.y = v.y * tsb.x + tsb.z;
			}
			const float c[4] = { v.x, v.y, v.z, v.w };
			for (int k = 0; k < comps; ++k) buf[(size_t)i * comps + k] = c[k];
		}
		if (!m_vbo[dt]) glGenBuffers(1, &m_vbo[dt]);
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo[dt]);
		glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), &buf[0], GL_STATIC_DRAW);
		m_vboComponents[dt] = comps;
	}
	if (indices() > 0) {
		uint16_t* idx = 0;
		int isize = 0;
		getIndexDataPtr(&idx, &isize);
		if (!m_ibo) glGenBuffers(1, &m_ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)indices() * 2, idx, GL_STATIC_DRAW);
	}
	DIPrimitive::unlock();
	m_dirty = false;
	checkGL("GLES2_Primitive::upload");
}

void GLES2_Primitive::sortTriangles() {
	GLES2_Shader* fx = m_ctx->currentShader;
	if (!fx || indices() < 3 || !m_ibo || m_type != PRIM_TRI) return;
	const Shader::SortType sort = fx->sort();
	if (sort != Shader::SORT_BACKTOFRONT && sort != Shader::SORT_FRONTTOBACK) return;
	const float4 cam = fx->getVector("CAMERAP");
	const float4x4 world = fx->getMatrix("WORLDTM");
	lock(LOCK_READWRITE);
	if (sort == Shader::SORT_BACKTOFRONT) sortBackToFront(float3(cam.x, cam.y, cam.z), world, 0, 0, m_sortBuffer);
	else sortFrontToBack(float3(cam.x, cam.y, cam.z), world, 0, 0, m_sortBuffer);
	uint16_t* idx = 0;
	int isize = 0;
	getIndexDataPtr(&idx, &isize);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (size_t)indices() * 2, idx);
	DIPrimitive::unlock();
}

void GLES2_Primitive::render() {
	const GLES2_PassProgram* p = m_ctx->currentPass;
	if (!p) return;
	if (m_dirty) upload();
	sortTriangles();
	const int vb = vertexRangeBegin(), ve = vertexRangeEnd();
	if (ve <= vb) return;

	static const float DEFAULTS[VertexFormat::DT_SIZE][4] = {
		{0,0,0,1}, {0,0,0,1}, {1,0,0,0}, {0,0,0,0}, {0,1,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {1,0,0,0}
	};
	for (int dt = 0; dt < VertexFormat::DT_SIZE; ++dt) {
		const GLint loc = p->attrib[dt];
		if (loc < 0) continue;
		if (m_vbo[dt]) {
			glBindBuffer(GL_ARRAY_BUFFER, m_vbo[dt]);
			glEnableVertexAttribArray(loc);
			glVertexAttribPointer(loc, m_vboComponents[dt], GL_FLOAT, GL_FALSE, 0, 0);
		} else if (dt == VertexFormat::DT_TEX1 && m_vbo[VertexFormat::DT_TEX0]) {
			glBindBuffer(GL_ARRAY_BUFFER, m_vbo[VertexFormat::DT_TEX0]);
			glEnableVertexAttribArray(loc);
			glVertexAttribPointer(loc, m_vboComponents[VertexFormat::DT_TEX0], GL_FLOAT, GL_FALSE, 0, 0);
		} else {
			glDisableVertexAttribArray(loc);
			glVertexAttrib4fv(loc, DEFAULTS[dt]);
			static bool warned[VertexFormat::DT_SIZE] = { false };
			if (!warned[dt]) {
				warned[dt] = true;
				fprintf(stderr, "[gles2] the shader uses %s but a primitive lacks it (format: %s)\n",
				        ATTRIB_NAMES[dt], vertexFormat().toString().c_str());
			}
		}
	}
	GLenum mode;
	switch (m_type) {
	case PRIM_POINT: case PRIM_SPRITE: mode = GL_POINTS; break;
	case PRIM_LINE: mode = GL_LINES; break;
	case PRIM_LINESTRIP: mode = GL_LINE_STRIP; break;
	case PRIM_TRISTRIP: mode = GL_TRIANGLE_STRIP; break;
	case PRIM_TRIFAN: mode = GL_TRIANGLE_FAN; break;
	default: mode = GL_TRIANGLES; break;
	}
	if (indices() > 0 && m_ibo) {
		const int ib = indexRangeBegin(), ie = indexRangeEnd();
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
		if (ie > ib) glDrawElements(mode, ie - ib, GL_UNSIGNED_SHORT, (const void*)(size_t)(ib * 2));
	} else {
		glDrawArrays(mode, vb, ve - vb);
	}
	for (int dt = 0; dt < VertexFormat::DT_SIZE; ++dt)
		if (p->attrib[dt] >= 0 && m_vbo[dt]) glDisableVertexAttribArray(p->attrib[dt]);
}

END_NAMESPACE()
