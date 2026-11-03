#include "Ui.h"
#include "ImageLoader.h"
#include <GLES2/gl2.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <algorithm>

namespace redbound {

static int u16(const std::vector<unsigned char>& b, size_t o) { return o + 1 < b.size() ? (b[o] << 8) | b[o + 1] : 0; }
static int s16(const std::vector<unsigned char>& b, size_t o) { const int v = u16(b, o); return v >= 0x8000 ? v - 0x10000 : v; }
static std::string str(const std::vector<unsigned char>& b, size_t& o) {
	const int n = u16(b, o);
	std::string s(b.begin() + o + 2, b.begin() + std::min(b.size(), o + 2 + n));
	o += 2 + n;
	return s;
}
static bool readFile(const std::string& p, std::vector<unsigned char>& out) {
	std::ifstream f(p.c_str(), std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}
static int glyphAdvance(const UiGlyph& g) { return g.right >= g.left ? (g.right - g.left + 1) + 1 : g.advance; }

static std::string dirOf(const std::string& p) { const size_t s = p.find_last_of("/\\"); return s == std::string::npos ? "" : p.substr(0, s + 1); }
static std::string stem(const std::string& p) {
	std::string b = p.substr(p.find_last_of("/\\") == std::string::npos ? 0 : p.find_last_of("/\\") + 1);
	return b.substr(0, b.find('.'));
}

Ui::~Ui() {
	for (auto& s : m_sheets) if (s.second.tex) glDeleteTextures(1, &s.second.tex);
	for (auto& f : m_fonts) if (f.second.tex) glDeleteTextures(1, &f.second.tex);
	if (m_prog) glDeleteProgram(m_prog);
	if (m_blurProg) glDeleteProgram(m_blurProg);
	if (m_capTex) glDeleteTextures(1, &m_capTex);
}

unsigned Ui::loadTexture(const std::string& png, int& w, int& h, std::vector<unsigned char>* rgbaOut) {
	Image im;
	if (!loadImage(png, im) || im.levels.empty()) { fprintf(stderr, "ui: could not load %s\n", png.c_str()); return 0; }
	const ImageLevel& l = im.levels[0];
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, l.width, l.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &l.rgba[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	w = l.width;
	h = l.height;
	if (rgbaOut) *rgbaOut = l.rgba;
	return t;
}

bool Ui::loadSheet(const std::string& datPath) {
	std::vector<unsigned char> b;
	if (!readFile(datPath, b)) return false;
	size_t o = 0;
	const std::string png = str(b, o);
	const int n = u16(b, o);
	o += 2;
	Sheet sh;
	for (int i = 0; i < n && o < b.size(); ++i) {
		const std::string name = str(b, o);
		UiSprite s = { s16(b, o), s16(b, o + 2), s16(b, o + 4), s16(b, o + 6), s16(b, o + 8), s16(b, o + 10) };
		o += 12;
		sh.sprites[name] = s;
	}
	sh.tex = loadTexture(dirOf(datPath) + png, sh.tw, sh.th);
	if (!sh.tex) return false;
	m_sheets[stem(datPath)] = sh;
	return true;
}

bool Ui::loadFont(const std::string& datPath) {
	std::vector<unsigned char> b;
	if (!readFile(datPath, b)) return false;
	size_t o = 0;
	const std::string png = str(b, o);
	Font f;
	f.lineHeight = u16(b, o);
	const int n = u16(b, o + 4);
	o += 6;
	for (int i = 0; i < n && o + 12 <= b.size(); ++i) {
		UiGlyph g = { s16(b, o + 2), s16(b, o + 4), s16(b, o + 6), s16(b, o + 8), s16(b, o + 10) };
		f.glyphs[u16(b, o)] = g;
		o += 12;
	}
	std::vector<unsigned char> px;
	f.tex = loadTexture(dirOf(datPath) + png, f.tw, f.th, &px);
	if (!f.tex) return false;
	for (auto& it : f.glyphs) {
		UiGlyph& g = it.second;
		g.left = g.w;
		g.right = -1;
		for (int x = 0; x < g.w; ++x)
			for (int y = 0; y < g.h; ++y) {
				const int ix = g.x + x, iy = g.y + y;
				if (ix < 0 || iy < 0 || ix >= f.tw || iy >= f.th) continue;
				if (px[((size_t)iy * f.tw + ix) * 4 + 3] > 24) { g.left = std::min(g.left, x); g.right = std::max(g.right, x); }
			}
		if (g.right < g.left) { g.left = 0; g.right = -1; }
	}
	m_fonts[stem(datPath)] = f;
	return true;
}

void Ui::initGl() {
	const char* vs = "attribute vec2 p; attribute vec2 t; varying vec2 v; uniform vec4 pos;\n"
	                 "void main() { v = t; gl_Position = vec4(pos.xy + p * pos.zw, 0.0, 1.0); }";
	const char* fs = "precision mediump float; varying vec2 v; uniform sampler2D tex; uniform float alpha;\n"
	                 "void main() { vec4 c = texture2D(tex, v); gl_FragColor = vec4(c.rgb, c.a * alpha); }";
	GLuint a = glCreateShader(GL_VERTEX_SHADER), c = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(a, 1, &vs, 0); glCompileShader(a);
	glShaderSource(c, 1, &fs, 0); glCompileShader(c);
	m_prog = glCreateProgram();
	glAttachShader(m_prog, a); glAttachShader(m_prog, c);
	glBindAttribLocation(m_prog, 0, "p");
	glBindAttribLocation(m_prog, 1, "t");
	glLinkProgram(m_prog);
	glDeleteShader(a); glDeleteShader(c);
	m_uPos = glGetUniformLocation(m_prog, "pos");
	m_uTex = glGetUniformLocation(m_prog, "tex");
	m_uAlpha = glGetUniformLocation(m_prog, "alpha");
}

void Ui::begin(int screenW, int screenH) {
	if (!m_prog) initGl();
	m_sw = screenW;
	m_sh = screenH > 0 ? screenH : 1;
	m_scale = m_sh / 480.f;
	m_vw = (int)(m_sw / m_scale + 0.5f);
	glUseProgram(m_prog);
	glUniform1i(m_uTex, 0);
	glActiveTexture(GL_TEXTURE0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
}

void Ui::end() {
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void Ui::quad(unsigned tex, int tw, int th, int sx, int sy, int sw, int sh, float x, float y, float w, float h, float alpha) {
	const float px = x * m_scale, py = y * m_scale, pw = w * m_scale, ph = h * m_scale;
	glUniform4f(m_uPos, px / m_sw * 2.f - 1.f, 1.f - py / m_sh * 2.f, pw / m_sw * 2.f, -ph / m_sh * 2.f);
	glUniform1f(m_uAlpha, alpha);
	glBindTexture(GL_TEXTURE_2D, tex);
	const float u0 = (float)sx / tw, v0 = (float)sy / th, u1 = (float)(sx + sw) / tw, v1 = (float)(sy + sh) / th;
	const float verts[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	const float uv[] = { u0, v0, u1, v0, u0, v1, u1, v1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Ui::drawSprite(const std::string& sheet, const std::string& name, float x, float y, float alpha, float scale) {
	auto sh = m_sheets.find(sheet);
	if (sh == m_sheets.end()) return;
	auto it = sh->second.sprites.find(name);
	if (it == sh->second.sprites.end()) return;
	const UiSprite& s = it->second;
	quad(sh->second.tex, sh->second.tw, sh->second.th, s.x, s.y, s.w, s.h, x - s.ax * scale, y - s.ay * scale, s.w * scale, s.h * scale, alpha);
}

int Ui::stringWidth(const std::string& font, const std::string& text) const {
	auto f = m_fonts.find(font);
	if (f == m_fonts.end()) return 0;
	int w = 0;
	for (unsigned char c : text) {
		auto g = f->second.glyphs.find(c);
		if (g != f->second.glyphs.end()) w += glyphAdvance(g->second);
	}
	return w;
}

void Ui::drawString(const std::string& font, const std::string& text, float x, float y, int halign, int valign, float alpha) {
	auto f = m_fonts.find(font);
	if (f == m_fonts.end()) return;
	const float w = (float)stringWidth(font, text);
	const float h = (float)f->second.lineHeight;
	float cx = halign == 1 ? x - w * 0.5f : halign == 2 ? x - w : x;
	const float cy = valign == 1 ? y - h * 0.5f : valign == 2 ? y - h : y;
	for (unsigned char c : text) {
		auto g = f->second.glyphs.find(c);
		if (g == f->second.glyphs.end()) continue;
		const UiGlyph& gl = g->second;
		if (gl.w > 0 && gl.h > 0 && gl.right >= gl.left)
			quad(f->second.tex, f->second.tw, f->second.th, gl.x, gl.y, gl.w, gl.h, cx - gl.left, cy + h - gl.h, (float)gl.w, (float)gl.h, alpha);
		cx += glyphAdvance(gl);
	}
}

int Ui::spriteWidth(const std::string& sheet, const std::string& name) const {
	auto sh = m_sheets.find(sheet);
	if (sh == m_sheets.end()) return 0;
	auto it = sh->second.sprites.find(name);
	return it == sh->second.sprites.end() ? 0 : it->second.w;
}

int Ui::spriteHeight(const std::string& sheet, const std::string& name) const {
	auto sh = m_sheets.find(sheet);
	if (sh == m_sheets.end()) return 0;
	auto it = sh->second.sprites.find(name);
	return it == sh->second.sprites.end() ? 0 : it->second.h;
}

void Ui::captureScreen() {
	if (m_sw <= 0 || m_sh <= 0) return;
	if (!m_capTex || m_capW != m_sw || m_capH != m_sh) {
		if (!m_capTex) glGenTextures(1, &m_capTex);
		glBindTexture(GL_TEXTURE_2D, m_capTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_sw, m_sh, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		m_capW = m_sw;
		m_capH = m_sh;
	}
	glBindTexture(GL_TEXTURE_2D, m_capTex);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_sw, m_sh);
	m_haveCap = true;
}

void Ui::drawCaptured(float blur, float saturation, float brightness) {
	if (!hasCapture()) return;
	if (!m_blurProg) {
		const char* vs = "attribute vec2 p; varying vec2 v; void main() { v = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
		const char* fs =
			"precision mediump float; varying vec2 v; uniform sampler2D tex; uniform vec2 r; uniform float sat; uniform float bri;\n"
			"void main() {\n"
			"  vec3 c = texture2D(tex, v).rgb; float w = 1.0;\n"
			"  for (int ring = 1; ring <= 3; ++ring) {\n"
			"    float d = float(ring) / 3.0; float k = 1.0 - d * 0.5;\n"
			"    for (int i = 0; i < 8; ++i) {\n"
			"      float a = (float(i) + float(ring) * 0.5) * 0.785398;\n"
			"      c += texture2D(tex, v + vec2(cos(a), sin(a)) * r * d).rgb * k; w += k;\n"
			"    }\n"
			"  }\n"
			"  c /= w;\n"
			"  float g = dot(c, vec3(0.299, 0.587, 0.114));\n"
			"  gl_FragColor = vec4(mix(vec3(g), c, sat) * bri, 1.0);\n"
			"}";
		GLuint a = glCreateShader(GL_VERTEX_SHADER), c = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(a, 1, &vs, 0); glCompileShader(a);
		glShaderSource(c, 1, &fs, 0); glCompileShader(c);
		GLint ok = 0;
		glGetShaderiv(c, GL_COMPILE_STATUS, &ok);
		if (!ok) { char log[1024]; glGetShaderInfoLog(c, sizeof(log), 0, log); fprintf(stderr, "ui: desenfoque: %s\n", log); }
		m_blurProg = glCreateProgram();
		glAttachShader(m_blurProg, a); glAttachShader(m_blurProg, c);
		glBindAttribLocation(m_blurProg, 0, "p");
		glLinkProgram(m_blurProg);
		glDeleteShader(a); glDeleteShader(c);
	}
	glUseProgram(m_blurProg);
	glUniform1i(glGetUniformLocation(m_blurProg, "tex"), 0);
	glUniform2f(glGetUniformLocation(m_blurProg, "r"), blur * m_scale / m_capW, blur * m_scale / m_capH);
	glUniform1f(glGetUniformLocation(m_blurProg, "sat"), saturation);
	glUniform1f(glGetUniformLocation(m_blurProg, "bri"), brightness);
	glBindTexture(GL_TEXTURE_2D, m_capTex);
	glDisable(GL_BLEND);
	const float verts[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glDisableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glEnable(GL_BLEND);
	glEnableVertexAttribArray(1);
	glUseProgram(m_prog);
}

bool Ui::hitSprite(const std::string& sheet, const std::string& name, float x, float y, float scale, float px, float py) const {
	auto sh = m_sheets.find(sheet);
	if (sh == m_sheets.end()) return false;
	auto it = sh->second.sprites.find(name);
	if (it == sh->second.sprites.end()) return false;
	const UiSprite& s = it->second;
	const float x0 = x - s.ax * scale, y0 = y - s.ay * scale;
	return px >= x0 && py >= y0 && px < x0 + s.w * scale && py < y0 + s.h * scale;
}

bool Ui::hasSprite(const std::string& sheet, const std::string& name) const {
	auto sh = m_sheets.find(sheet);
	return sh != m_sheets.end() && sh->second.sprites.count(name);
}

}
