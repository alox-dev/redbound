#pragma once
#include <map>
#include <string>
#include <vector>

namespace redbound {

struct UiSprite { int x, y, w, h, ax, ay; };
struct UiGlyph { int x, y, w, h, advance; int left = 0, right = 0; };

class Ui {
public:
	~Ui();
	bool loadSheet(const std::string& datPath);
	bool loadFont(const std::string& datPath);

	void begin(int screenW, int screenH);
	void end();
	int width() const { return m_vw; }
	int height() const { return 480; }

	void drawSprite(const std::string& sheet, const std::string& name, float x, float y, float alpha = 1.f, float scale = 1.f);
	void drawString(const std::string& font, const std::string& text, float x, float y, int halign, int valign, float alpha = 1.f);
	int spriteWidth(const std::string& sheet, const std::string& name) const;
	int spriteHeight(const std::string& sheet, const std::string& name) const;
	int stringWidth(const std::string& font, const std::string& text) const;
	bool hasSprite(const std::string& sheet, const std::string& name) const;
	void captureScreen();
	bool hasCapture() const { return m_capTex != 0 && m_haveCap; }
	void clearCapture() { m_haveCap = false; }
	void drawCaptured(float blur, float saturation, float brightness);
	bool hitSprite(const std::string& sheet, const std::string& name, float x, float y, float scale, float px, float py) const;

private:
	struct Sheet { unsigned tex = 0; int tw = 0, th = 0; std::map<std::string, UiSprite> sprites; };
	struct Font { unsigned tex = 0; int tw = 0, th = 0; int lineHeight = 0; std::map<int, UiGlyph> glyphs; };
	std::map<std::string, Sheet> m_sheets;
	std::map<std::string, Font> m_fonts;
	unsigned m_prog = 0;
	unsigned m_blurProg = 0, m_capTex = 0;
	int m_capW = 0, m_capH = 0;
	bool m_haveCap = false;
	int m_uPos = -1, m_uTex = -1, m_uAlpha = -1;
	int m_sw = 0, m_sh = 0, m_vw = 800;
	float m_scale = 1.f;
	void initGl();
	unsigned loadTexture(const std::string& png, int& w, int& h, std::vector<unsigned char>* rgbaOut = 0);
	void quad(unsigned tex, int tw, int th, int sx, int sy, int sw, int sh, float x, float y, float w, float h, float alpha);
};

}
