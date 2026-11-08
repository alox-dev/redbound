#include "Overlay.h"
#include "Config.h"
#include "DebImport.h"
#include "Audio.h"
#include "AndroidBridge.h"
#include "I18n.h"
#include <dirent.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
#include <cmath>
#include <algorithm>
#include <sys/stat.h>
#include <vector>
#include <cfloat>
#include <cstdint>
#include <GLES2/gl2.h>
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "Paths.h"
#include "Records.h"
#include "Icons.h"
#include "UpdateCheck.h"
#include <ctime>

namespace redbound {
static bool mergeIconFont(const char* relative, const ImWchar* ranges, float size);
static std::string format(const char* fmt, ...);
static std::string g_pressedSlider, g_restoreSlider;
static float g_pressedValue = 0.f;
}

namespace redbound {

static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static std::string findFont() {
	if (const char* f = getenv("REDBOUND_FONT")) return f;
#ifdef _WIN32
	const std::string windir = getenv("WINDIR") ? getenv("WINDIR") : "C:\\Windows";
	const std::string fonts[] = { windir + "\\Fonts\\segoeui.ttf", windir + "\\Fonts\\arial.ttf", windir + "\\Fonts\\tahoma.ttf" };
	for (const std::string& f : fonts) if (exists(f)) return f;
	return "";
#elif defined(__ANDROID__)
	const char* fonts[] = { "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/SamsungOneUI-Regular.ttf",
	                        "/system/fonts/SECRobotoLight-Regular.ttf", "/system/fonts/DroidSans.ttf" };
	for (const char* f : fonts) if (exists(f)) return f;
	return "";
#else
	std::string out;
	if (FILE* p = popen("fc-match -f '%{file}' 'sans:style=Regular' 2>/dev/null", "r")) {
		char buf[1024];
		if (fgets(buf, sizeof(buf), p)) out = buf;
		pclose(p);
	}
	if (!out.empty() && exists(out)) return out;
	const char* fallbacks[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
	                            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf" };
	for (const char* f : fallbacks) if (exists(f)) return f;
	return "";
#endif
}

static std::string pickDeb() {
#ifdef _WIN32
	char path[MAX_PATH] = "";
	OPENFILENAMEA ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = "Bounce Evolution package (*.deb)\0*.deb\0All files (*.*)\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = sizeof(path);
	ofn.lpstrTitle = tr("game.picker_title");
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	return GetOpenFileNameA(&ofn) ? std::string(path) : std::string();
#elif defined(__ANDROID__)
	return "";
#else
	const std::string commands[] = { std::string("zenity --file-selection --title='") + tr("game.picker_title") + "' --file-filter='*.deb' 2>/dev/null",
	                                 "kdialog --getopenfilename ~ '*.deb' 2>/dev/null" };
	for (const std::string& c : commands) {
		if (FILE* p = popen(c.c_str(), "r")) {
			char buf[2048];
			std::string out;
			if (fgets(buf, sizeof(buf), p)) out = buf;
			const int rc = pclose(p);
			while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
			if (!out.empty() && rc == 0) return out;
			if (rc == 0 || rc == 256) return "";
		}
	}
	return "";
#endif
}

bool Overlay::init(SDL_Window* win, SDL_GLContext ctx) {
	m_win = win;
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = 0;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	int w, h;
	SDL_GetWindowSize(win, &w, &h);
	float ddpi = 0.f;
	if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &ddpi, 0, 0) != 0 || ddpi < 50.f) ddpi = 96.f;
	int dw, dh;
	SDL_GL_GetDrawableSize(win, &dw, &dh);
	const float pxPerMm = ddpi / 25.4f * (w > 0 ? (float)dw / w : 1.f);
	float fontPx = std::max(3.4f * pxPerMm, 22.f * (dh > 0 ? dh / 720.f : 1.f) * 0.8f);
	m_scale = fontPx / 22.f;
	const bool touch = SDL_GetNumTouchDevices() > 0;
	const float diagonalIn = std::sqrt((float)dw * dw + (float)dh * dh) / (pxPerMm * 25.4f);
	m_compact = touch || diagonalIn < 9.f || getenv("REDBOUND_MENU_COMPACT");
	if (m_compact) fontPx = std::max(fontPx, 18.f);
	m_scale = fontPx / 22.f;
	const std::string font = findFont();
	if (!font.empty()) io.Fonts->AddFontFromFileTTF(font.c_str(), fontPx, 0, io.Fonts->GetGlyphRangesDefault());
	else {
		io.Fonts->AddFontDefault();
		io.FontGlobalScale = std::max(1.f, m_scale * 1.5f);
	}
	static const ImWchar brands[] = { 0xf09b, 0xf09b, 0xf167, 0xf167, 0xf392, 0xf392, 0 };
	static const ImWchar solid[] = { 0xf00c, 0xf00c, 0xf030, 0xf030, 0xf05a, 0xf05a, 0xf0aa, 0xf0aa, 0xf11b, 0xf11c, 0xf1f8, 0xf1f8,
	                                 0xf25a, 0xf25a, 0xf3cf, 0xf3cf, 0xf7b6, 0xf7b6, 0xf8cc, 0xf8cc, 0 };
	const float iconPx = font.empty() ? 13.f : fontPx;
	m_icons = mergeIconFont("fonts/icons-brands.ttf", brands, iconPx) && mergeIconFont("fonts/icons-solid.ttf", solid, iconPx);
	const float scale = m_scale;
	ImGui::StyleColorsDark();
	ImGuiStyle& st = ImGui::GetStyle();
	st.WindowRounding = 12.f;
	st.FrameRounding = 8.f;
	st.GrabRounding = 8.f;
	st.TabRounding = 8.f;
	st.WindowPadding = ImVec2(18, 16);
	st.ItemSpacing = ImVec2(10, 10);
	st.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.09f, 0.16f, 0.94f);
	st.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.20f, 0.36f, 1.f);
	st.Colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.36f, 0.62f, 1.f);
	st.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.28f, 0.50f, 1.f);
	st.ScaleAllSizes(std::max(0.8f, scale));
	if (m_compact) {
		st.FramePadding = ImVec2(5.f * pxPerMm / 2.f, 2.6f * pxPerMm);
		st.ItemSpacing = ImVec2(2.5f * pxPerMm, 2.5f * pxPerMm);
		st.ScrollbarSize = 4.f * pxPerMm;
		st.TouchExtraPadding = ImVec2(1.f * pxPerMm, 1.f * pxPerMm);
	}
	if (!ImGui_ImplSDL2_InitForOpenGL(win, ctx)) return false;
	if (!ImGui_ImplOpenGL3_Init("#version 100")) return false;
	loadTitle();
	m_ready = true;
	if (Config::get().flag("general.check_updates", true)) startUpdateCheck(REDBOUND_VERSION);
	Strings::get().load(Config::get().str("general.language", "auto"));
	printf("port menu: ready (Esc, Back/View/- or a three finger tap), %.0f dpi, font %.0f px%s%s, language %s\n", ddpi, fontPx,
	       m_compact ? ", compact" : "", font.empty() ? " (no system font)" : "", Strings::get().current().c_str());
	return true;
}

void Overlay::shutdown() {
	if (m_titleTex) {
		GLuint tex = m_titleTex;
		glDeleteTextures(1, &tex);
		m_titleTex = 0;
	}
	if (!m_ready) return;
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	m_ready = false;
}

bool Overlay::processEvent(const SDL_Event& e) {
	if (!m_ready) return false;
	bool toggle = false;
	if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_AC_BACK) && !e.key.repeat) toggle = true;
	if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) toggle = true;
	if (e.type == SDL_FINGERDOWN && SDL_GetNumTouchFingers(e.tfinger.touchId) >= 3) toggle = true;
	if (toggle) { if (!m_importMode) m_open = !m_open; return true; }
#ifndef __ANDROID__
	if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_F11) {
		const bool full = (SDL_GetWindowFlags(m_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
		SDL_SetWindowFullscreen(m_win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		Config::get().setFlag("graphics.fullscreen", full);
		Config::get().save();
		return true;
	}
	if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_F2) {
		m_screenshotRequested = true;
		return true;
	}
#endif
	if (!m_open) return false;
	ImGui_ImplSDL2_ProcessEvent(&e);
	switch (e.type) {
	case SDL_QUIT: case SDL_WINDOWEVENT: return false;
	default: return true;
	}
}

std::string Overlay::saveScreenshot() {
	int w = 0, h = 0;
	SDL_GL_GetDrawableSize(m_win, &w, &h);
	if (w <= 0 || h <= 0) return std::string();
	std::vector<unsigned char> pixels((size_t)w * h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
	std::vector<unsigned char> rows((size_t)w * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			for (int c = 0; c < 3; ++c) rows[((size_t)(h - 1 - y) * w + x) * 3 + c] = pixels[((size_t)y * w + x) * 4 + c];
	const std::string dir = picturesDir();
	makeDirectories(dir);
	const time_t now = time(0);
	char name[64];
	strftime(name, sizeof(name), "redbound-%Y%m%d-%H%M%S.png", localtime(&now));
	const std::string path = dir + "/" + name;
	if (!stbi_write_png(path.c_str(), w, h, 3, &rows[0], w * 3)) {
		printf("screenshot: could not write %s\n", path.c_str());
		return std::string();
	}
	printf("screenshot: %s\n", path.c_str());
	notify(tr("notice.screenshot"), icon::camera);
	return path;
}

float Overlay::labelWidth(const std::string& text, float heightPx) const {
	if (!m_ready) return 0.f;
	ImFont* font = ImGui::GetIO().Fonts->Fonts.empty() ? nullptr : ImGui::GetIO().Fonts->Fonts[0];
	if (!font) return 0.f;
	return font->CalcTextSizeA(heightPx, FLT_MAX, 0.f, text.c_str()).x;
}

void Overlay::drawLabels() {
	ImDrawList* list = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	for (const Label& l : m_labels) {
		const ImVec2 size = font->CalcTextSizeA(l.heightPx, FLT_MAX, 0.f, l.text.c_str());
		const ImVec2 pos(l.rightPx - size.x, l.centerYPx - size.y * 0.5f);
		const float shadow = std::max(1.f, l.heightPx * 0.08f);
		list->AddText(font, l.heightPx, ImVec2(pos.x + shadow, pos.y + shadow), IM_COL32(20, 30, 45, 220), l.text.c_str());
		list->AddText(font, l.heightPx, pos, IM_COL32(255, 255, 255, 255), l.text.c_str());
	}
}

void Overlay::notify(const std::string& text, const char* glyph, float seconds) {
	for (Notice& n : m_notices)
		if (n.text == text) {
			n.age = std::min(n.age, 0.2f);
			return;
		}
	if (m_notices.size() >= 4) m_notices.erase(m_notices.begin());
	m_notices.push_back({ text, glyph ? glyph : icon::info, 0.f, seconds });
	printf("notice: %s\n", text.c_str());
}

void Overlay::drawNotices() {
	if (m_notices.empty()) return;
	const Uint32 now = SDL_GetTicks();
	const float dt = m_lastNoticeTicks ? std::min(0.1f, (now - m_lastNoticeTicks) / 1000.f) : 0.f;
	m_lastNoticeTicks = now;
	ImDrawList* list = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float size = ImGui::GetFontSize() * 0.95f;
	const float pad = 10.f * m_scale, gap = 8.f * m_scale, margin = 16.f * m_scale, accent = 3.f * m_scale;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	float y = margin;
	for (Notice& n : m_notices) {
		n.age += dt;
		const float in = std::min(1.f, n.age / 0.25f), out = std::min(1.f, std::max(0.f, (n.life - n.age) / 0.4f));
		const float alpha = std::min(in, out), ease = 1.f - (1.f - in) * (1.f - in);
		if (alpha <= 0.f) continue;
		const ImVec2 iconSize = m_icons ? font->CalcTextSizeA(size, FLT_MAX, 0.f, n.icon.c_str()) : ImVec2(0, 0);
		const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.f, n.text.c_str());
		const float iconWidth = m_icons ? iconSize.x + pad * 0.8f : 0.f;
		const float w = pad * 2.f + accent + iconWidth + textSize.x, h = pad * 1.6f + textSize.y;
		const float x = display.x - margin - w + (1.f - ease) * 24.f * m_scale;
		const ImU32 a = (ImU32)(alpha * 255.f);
		list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(16, 24, 38, (int)(a * 0.9f)), 8.f * m_scale);
		list->AddRectFilled(ImVec2(x, y), ImVec2(x + accent, y + h), IM_COL32(242, 89, 77, a), 8.f * m_scale, ImDrawFlags_RoundCornersLeft);
		float tx = x + accent + pad;
		const float ty = y + (h - textSize.y) * 0.5f;
		if (m_icons) {
			list->AddText(font, size, ImVec2(tx, ty), IM_COL32(242, 89, 77, a), n.icon.c_str());
			tx += iconWidth;
		}
		list->AddText(font, size, ImVec2(tx, ty), IM_COL32(255, 255, 255, a), n.text.c_str());
		y += h * std::min(1.f, out * 2.f) + gap;
	}
	m_notices.erase(std::remove_if(m_notices.begin(), m_notices.end(), [](const Notice& n) { return n.age >= n.life; }), m_notices.end());
	if (m_notices.empty()) m_lastNoticeTicks = 0;
}

void Overlay::draw() {
	if (!m_ready) return;
	if (!m_updateNotified && updateState() == UpdateState::Available) {
		m_updateNotified = true;
		notify(format(tr("notice.update_available"), latestVersion().c_str()), icon::update, 7.f);
	}
	if (m_open) drawMenu();
	else if (!m_labels.empty() || !m_notices.empty()) {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
		drawLabels();
		drawNotices();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
	m_labels.clear();
	if (m_screenshotRequested) {
		m_screenshotRequested = false;
		saveScreenshot();
	}
}

void Overlay::dragScroll() {
	const ImGuiIO& io = ImGui::GetIO();
	const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		m_scrollCandidate = hovered;
		m_scrollDragging = false;
		if (hovered) m_scrollVelocity = 0.f;
	}
	if (m_scrollCandidate && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		const ImVec2 total = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
		if (!m_scrollDragging && std::fabs(total.y) > 10.f * m_scale && std::fabs(total.y) > std::fabs(total.x)) {
			m_scrollDragging = true;
			if (!g_pressedSlider.empty()) g_restoreSlider = g_pressedSlider;
		}
		if (m_scrollDragging) {
			ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
			if (io.DeltaTime > 0.f) m_scrollVelocity = m_scrollVelocity * 0.6f + (-io.MouseDelta.y / io.DeltaTime) * 0.4f;
			ImGui::ClearActiveID();
		}
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		g_pressedSlider.clear();
		if (!m_scrollDragging) m_scrollVelocity = 0.f;
		m_scrollCandidate = m_scrollDragging = false;
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && std::fabs(m_scrollVelocity) > 5.f) {
		ImGui::SetScrollY(ImGui::GetScrollY() + m_scrollVelocity * io.DeltaTime);
		m_scrollVelocity *= expf(-io.DeltaTime * 4.f);
		if (ImGui::GetScrollY() <= 0.f || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) m_scrollVelocity = 0.f;
	}
}

void Overlay::drawMenu() {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 size = m_compact ? io.DisplaySize
	                              : ImVec2(std::min(io.DisplaySize.x * 0.9f, 900.f * m_scale), std::min(io.DisplaySize.y * 0.86f, 640.f * m_scale));
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::Begin("Redbound", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
	                                     (m_compact ? ImGuiWindowFlags_NoTitleBar : 0));
	const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	auto content = [&](const std::function<void()>& body) {
		ImGui::BeginChild("content", ImVec2(0, -footer), ImGuiChildFlags_None);
		body();
		dragScroll();
		ImGui::EndChild();
	};
	auto tab = [](const char* key, const char* id) { return std::string(tr(key)) + "###" + id; };
	if (m_importMode) {
		content([this] { drawImport(); });
	} else if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
		if (ImGui::BeginTabItem(tab("tab.game", "game").c_str())) { content([this] { drawGame(); }); ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem(tab("tab.controls", "controls").c_str())) { content([this] { drawControls(); }); ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem(tab("tab.graphics", "graphics").c_str())) { content([this] { drawGraphics(); }); ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem(tab("tab.audio", "audio").c_str())) { content([this] { drawAudio(); }); ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem(tab("tab.improvements", "improvements").c_str())) { content([this] { drawImprovements(); }); ImGui::EndTabItem(); }
		if (ImGui::BeginTabItem(tab("tab.about", "about").c_str())) { content([this] { drawAbout(); }); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}
	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - ImGui::GetStyle().WindowPadding.y);
	const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (m_importMode) {
		if (ImGui::Button(tr("button.quit"), ImVec2(-1, 0)) && onQuit) onQuit();
	} else {
		if (ImGui::Button(tr("button.continue"), ImVec2(bw, 0))) m_open = false;
		ImGui::SameLine();
		if (ImGui::Button(tr("button.quit_game"), ImVec2(bw, 0)) && onQuit) onQuit();
	}
	ImGui::End();
	drawLabels();
	drawNotices();
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static std::string format(const char* fmt, ...) {
	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	return buffer;
}

void Overlay::importFrom(const std::string& deb) {
	DebImportResult r = importDeb(deb, defaultGameDir());
	if (r.ok) {
		m_status = format(tr("game.imported"), r.files) + (m_importMode ? "" : std::string(" ") + tr("game.restart_needed"));
		if (!r.knownVersion) m_status += std::string("\n") + format(tr("game.unknown_version"), r.sha256.c_str());
	}
	else m_status = format(tr("game.import_failed"), deb.c_str(), r.error.c_str());
}

static const ImVec4 kAccent(0.95f, 0.35f, 0.30f, 1.f);

static void section(const char* title) {
	ImGui::Spacing();
	ImGui::TextColored(kAccent, "%s", title);
	ImGui::Separator();
	ImGui::Spacing();
}

static bool beginRows(const char* id) {
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
	ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.45f);
	ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
	return true;
}

static void row(const char* label) {
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-FLT_MIN);
}

static bool checkRow(const char* label, const char* key, bool fallback) {
	Config& c = Config::get();
	bool v = c.flag(key, fallback);
	row(label);
	ImGui::PushID(key);
	const bool changed = ImGui::Checkbox("##v", &v);
	ImGui::PopID();
	if (changed) {
		c.setFlag(key, v);
		c.save();
	}
	return changed;
}

static bool sliderRow(const char* label, const char* key, float fallback, float lo, float hi, const char* fmt) {
	Config& c = Config::get();
	if (g_restoreSlider == key) {
		g_restoreSlider.clear();
		c.setNum(key, g_pressedValue);
		c.save();
		row(label);
		float v = g_pressedValue;
		ImGui::PushID(key);
		ImGui::SliderFloat("##v", &v, lo, hi, fmt);
		ImGui::PopID();
		return true;
	}
	const float before = c.num(key, fallback);
	float v = before;
	row(label);
	ImGui::PushID(key);
	const bool changed = ImGui::SliderFloat("##v", &v, lo, hi, fmt);
	if (ImGui::IsItemActivated()) {
		g_pressedSlider = key;
		g_pressedValue = before;
	}
	ImGui::PopID();
	if (changed) {
		c.setNum(key, v);
		c.save();
	}
	return changed;
}

static void centered(float width) {
	const float avail = ImGui::GetContentRegionAvail().x;
	if (avail > width) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
}

static void centeredText(const char* text, bool disabled = false) {
	centered(ImGui::CalcTextSize(text).x);
	if (disabled) ImGui::TextDisabled("%s", text);
	else ImGui::TextUnformatted(text);
}

static std::vector<unsigned char> readAsset(const std::string& relative) {
	std::vector<std::string> paths;
#ifdef __ANDROID__
	paths.push_back(relative);
#else
	if (char* base = SDL_GetBasePath()) {
		paths.push_back(std::string(base) + "assets/" + relative);
		SDL_free(base);
	}
#ifdef REDBOUND_ASSETS_DIR
	paths.push_back(std::string(REDBOUND_ASSETS_DIR) + "/" + relative);
#endif
#endif
	for (const std::string& path : paths) {
		SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
		if (!rw) continue;
		const Sint64 size = SDL_RWsize(rw);
		std::vector<unsigned char> bytes(size > 0 ? (size_t)size : 0);
		const bool read = size > 0 && SDL_RWread(rw, &bytes[0], 1, (size_t)size) == (size_t)size;
		SDL_RWclose(rw);
		if (read) return bytes;
	}
	return std::vector<unsigned char>();
}

static bool mergeIconFont(const char* relative, const ImWchar* ranges, float size) {
	std::vector<unsigned char> bytes = readAsset(relative);
	if (bytes.empty()) return false;
	void* data = IM_ALLOC(bytes.size());
	memcpy(data, &bytes[0], bytes.size());
	ImFontConfig cfg;
	cfg.MergeMode = true;
	cfg.PixelSnapH = true;
	cfg.GlyphMinAdvanceX = size;
	cfg.FontDataOwnedByAtlas = true;
	return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, (int)bytes.size(), size * 0.9f, &cfg, ranges) != nullptr;
}

void Overlay::loadTitle() {
	{
		std::vector<unsigned char> bytes = readAsset("title/title.png");
		if (bytes.empty()) return;
		int w = 0, h = 0, n = 0;
		unsigned char* pixels = stbi_load_from_memory(&bytes[0], (int)bytes.size(), &w, &h, &n, 4);
		if (!pixels) return;
		std::vector<unsigned char> image(pixels, pixels + (size_t)w * h * 4);
		stbi_image_free(pixels);
		while (w > 1100 && w >= 2 && h >= 2) {
			const int nw = w / 2, nh = h / 2;
			std::vector<unsigned char> half((size_t)nw * nh * 4);
			for (int y = 0; y < nh; ++y)
				for (int x = 0; x < nw; ++x)
					for (int ch = 0; ch < 4; ++ch) {
						const auto at = [&](int xx, int yy) { return (int)image[((size_t)yy * w + xx) * 4 + ch]; };
						half[((size_t)y * nw + x) * 4 + ch] = (unsigned char)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
					}
			image.swap(half);
			w = nw;
			h = nh;
		}
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &image[0]);
		m_titleTex = tex;
		m_titleW = w;
		m_titleH = h;
	}
}

void Overlay::drawTitle(float maxWidth) {
	if (!m_titleTex) {
		ImGui::SetWindowFontScale(2.f);
		centeredText("Redbound");
		ImGui::SetWindowFontScale(1.f);
		return;
	}
	const float width = std::min(maxWidth, ImGui::GetContentRegionAvail().x);
	const float height = width * (float)m_titleH / (float)m_titleW;
	centered(width);
	ImGui::Image((ImTextureID)(intptr_t)m_titleTex, ImVec2(width, height));
}

void Overlay::drawLanguage() {
	Config& c = Config::get();
	const std::string selected = c.str("general.language", "auto");
	const auto languages = Strings::get().available();
	std::string preview = tr("game.language_auto");
	for (const auto& l : languages) if (l.first == selected) preview = l.second;
	row(tr("game.language"));
	if (ImGui::BeginCombo("##language", preview.c_str())) {
		if (ImGui::Selectable(tr("game.language_auto"), selected == "auto")) {
			c.set("general.language", "auto");
			c.save();
			Strings::get().load("auto");
		}
		for (const auto& l : languages)
			if (ImGui::Selectable(l.second.c_str(), selected == l.first)) {
				c.set("general.language", l.first);
				c.save();
				Strings::get().load(l.first);
			}
		ImGui::EndCombo();
	}
}

void Overlay::drawImportButton(bool ok) {
	const char* button = ok ? tr("game.import_another") : tr("game.choose_deb");
	const float width = m_compact ? -FLT_MIN : std::max(ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 4.f, 260.f * m_scale);
	if (!m_compact) centered(width);
#ifdef __ANDROID__
	if (ImGui::Button(button, ImVec2(width, 0))) {
		m_waitPick = androidPickDeb();
		m_status = m_waitPick ? tr("game.picker_waiting") : tr("game.picker_failed");
	}
	if (m_waitPick) {
		const std::string picked = androidTakePicked();
		if (!picked.empty()) {
			m_waitPick = false;
			m_status = tr("game.importing");
			importFrom(picked);
			remove(picked.c_str());
		} else if (!androidPicking()) {
			m_waitPick = false;
			m_status = tr("game.nothing_chosen");
		}
	}
	const std::string external = androidExternalDir();
	if (!ok && !external.empty()) {
		m_scanTimer -= ImGui::GetIO().DeltaTime;
		if (m_scanTimer <= 0.f) {
			m_scanTimer = 1.5f;
			if (DIR* d = opendir(external.c_str())) {
				while (struct dirent* entry = readdir(d)) {
					const std::string name = entry->d_name;
					if (name.size() > 4 && name.compare(name.size() - 4, 4, ".deb") == 0) {
						importFrom(external + "/" + name);
						break;
					}
				}
				closedir(d);
			}
		}
	}
#else
	if (ImGui::Button(button, ImVec2(width, 0))) {
		const std::string deb = pickDeb();
		if (!deb.empty()) importFrom(deb);
		else m_status = tr("game.no_picker");
	}
#endif
	if (!m_status.empty()) {
		ImGui::Spacing();
		ImGui::PushTextWrapPos(0.f);
		centeredText(m_status.c_str(), true);
		ImGui::PopTextWrapPos();
	}
}

void Overlay::drawImport() {
	const bool ok = gameDataPresent(defaultGameDir());
	ImGui::Spacing();
	drawTitle(520.f * m_scale);
	ImGui::Spacing();
	ImGui::Spacing();
	centeredText(tr("game.welcome"));
	ImGui::Spacing();
	ImGui::Spacing();
	drawImportButton(ok);
#ifdef __ANDROID__
	const std::string external = androidExternalDir();
	if (!ok && !external.empty()) {
		ImGui::Spacing();
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled(tr("game.copy_by_usb"), external.c_str());
		ImGui::PopTextWrapPos();
	}
#endif
	ImGui::Spacing();
	section(tr("game.section_language"));
	if (beginRows("language")) {
		drawLanguage();
		ImGui::EndTable();
	}
}

void Overlay::drawGame() {
	const std::string dir = defaultGameDir();
	const bool ok = gameDataPresent(dir);
	section(tr("game.section_data"));
	if (beginRows("data")) {
		row(tr("game.status"));
		ImGui::TextColored(ok ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(1, 0.5f, 0.4f, 1), "%s", ok ? tr("game.data_ok") : tr("game.data_missing"));
		row(tr("game.folder"));
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextDisabled("%s", dir.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTable();
	}
	ImGui::Spacing();
	drawImportButton(ok);
	section(tr("game.section_updates"));
	if (beginRows("updates")) {
		if (checkRow(tr("game.check_updates"), "general.check_updates", true) && Config::get().flag("general.check_updates", true))
			startUpdateCheck(REDBOUND_VERSION);
		row(tr("game.update_status"));
		const UpdateState state = updateState();
		if (!Config::get().flag("general.check_updates", true) && state == UpdateState::Idle) ImGui::TextDisabled("%s", tr("game.update_disabled"));
		else if (state == UpdateState::Checking || state == UpdateState::Idle) ImGui::TextDisabled("%s", tr("game.update_checking"));
		else if (state == UpdateState::UpToDate) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "%s", tr("game.update_latest"));
		else if (state == UpdateState::Failed) ImGui::TextDisabled("%s", tr("game.update_failed"));
		else {
			ImGui::TextColored(kAccent, tr("game.update_available"), latestVersion().c_str());
			row("");
			const std::string label = m_icons ? std::string(icon::update) + "  " + tr("game.update_download") : std::string(tr("game.update_download"));
			if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) SDL_OpenURL(latestReleaseUrl().c_str());
		}
		ImGui::EndTable();
	}
	section(tr("game.section_records"));
	const std::vector<int> records = arcadeRecords();
	if (beginRows("records")) {
		for (size_t i = 0; i < records.size(); ++i) {
			char place[16], value[32];
			snprintf(place, sizeof(place), "%d.", (int)i + 1);
			snprintf(value, sizeof(value), "%d m", records[i]);
			row(place);
			ImGui::TextUnformatted(value);
		}
		ImGui::EndTable();
	}
	if (!arcadeRecordsAreDefault()) {
		ImGui::Spacing();
		const char* label = m_confirmClear ? tr("game.records_confirm") : tr("game.records_clear");
		if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
			if (m_confirmClear) {
				clearArcadeRecords();
				m_confirmClear = false;
				notify(tr("notice.records_cleared"), icon::trash);
			} else {
				m_confirmClear = true;
			}
		}
	} else {
		m_confirmClear = false;
	}
	section(tr("game.section_language"));
	if (beginRows("language")) {
		drawLanguage();
		ImGui::EndTable();
	}
}

void Overlay::drawControls() {
	Config& c = Config::get();
	section(tr("controls.section_camera"));
	if (beginRows("camera")) {
		sliderRow(tr("controls.camera_sensitivity"), "controls.camera_sensitivity", 1.f, 0.3f, 3.f, "%.1f");
		checkRow(tr("controls.invert_y"), "controls.invert_y", false);
		ImGui::EndTable();
	}
	section(tr("controls.section_controllers"));
	if (beginRows("controllers")) {
		sliderRow(tr("controls.deadzone"), "controls.deadzone", 0.22f, 0.05f, 0.5f, "%.2f");
		checkRow(tr("controls.vibration"), "controls.vibration", true);
		row(tr("controls.connected"));
		int n = 0;
		for (int i = 0; i < SDL_NumJoysticks(); ++i)
			if (SDL_IsGameController(i)) {
				ImGui::TextUnformatted(SDL_GameControllerNameForIndex(i));
				++n;
			}
		if (!n) ImGui::TextDisabled("%s", tr("controls.none_connected"));
		ImGui::EndTable();
	}
#ifndef __ANDROID__
	if (SDL_GetNumTouchDevices() == 0) return;
#endif
	section(tr("controls.section_touch"));
	if (beginRows("touch")) {
		const std::string scheme = c.str("controls.touch", "modern");
		const char* keys[] = { "modern", "classic", "accelerometer" };
		const char* labels[] = { tr("controls.touch_modern"), tr("controls.touch_classic"), tr("controls.touch_accelerometer") };
		int current = 0;
		for (int i = 0; i < 3; ++i) if (scheme == keys[i]) current = i;
		row(tr("controls.touch"));
		if (ImGui::BeginCombo("##touch", labels[current])) {
			for (int i = 0; i < 3; ++i)
				if (ImGui::Selectable(labels[i], i == current)) {
					c.set("controls.touch", keys[i]);
					c.save();
				}
			ImGui::EndCombo();
		}
		if (scheme == "accelerometer") {
			sliderRow(tr("controls.tilt_sensitivity"), "controls.tilt_sensitivity", 1.f, 0.4f, 2.5f, "%.1f");
			checkRow(tr("controls.tilt_invert_x"), "controls.tilt_invert_x", false);
			checkRow(tr("controls.tilt_invert_y"), "controls.tilt_invert_y", false);
			const std::string cam = c.str("controls.tilt_camera", "follow");
			row(tr("controls.tilt_camera"));
			if (ImGui::BeginCombo("##tilt_camera", cam == "fixed" ? tr("controls.tilt_camera_fixed") : tr("controls.tilt_camera_follow"))) {
				if (ImGui::Selectable(tr("controls.tilt_camera_follow"), cam != "fixed")) { c.set("controls.tilt_camera", "follow"); c.save(); }
				if (ImGui::Selectable(tr("controls.tilt_camera_fixed"), cam == "fixed")) { c.set("controls.tilt_camera", "fixed"); c.save(); }
				ImGui::EndCombo();
			}
			row("");
			if (ImGui::Button(tr("controls.calibrate"), ImVec2(-FLT_MIN, 0)) && onCalibrate) {
				onCalibrate();
				notify(tr("controls.calibrated"), icon::phone);
			}
		}
		ImGui::EndTable();
	}
}

void Overlay::drawGraphics() {
	Config& c = Config::get();
	section(tr("graphics.section_display"));
	if (beginRows("display")) {
		bool full = (SDL_GetWindowFlags(m_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
		row(tr("graphics.fullscreen"));
		if (ImGui::Checkbox("##fullscreen", &full)) {
			SDL_SetWindowFullscreen(m_win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
			c.setFlag("graphics.fullscreen", full);
			c.save();
		}
		if (checkRow(tr("graphics.vsync"), "graphics.vsync", true)) SDL_GL_SetSwapInterval(c.flag("graphics.vsync", true) ? 1 : 0);
		int w, h;
		SDL_GL_GetDrawableSize(m_win, &w, &h);
		row(tr("graphics.resolution"));
		ImGui::TextDisabled("%d x %d", w, h);
		ImGui::EndTable();
	}
	section(tr("graphics.section_performance"));
	if (beginRows("performance")) {
		if (g_restoreSlider == "graphics.render_scale") {
			g_restoreSlider.clear();
			c.setNum("graphics.render_scale", g_pressedValue);
			c.save();
		}
		const float scaleBefore = c.num("graphics.render_scale", 1.f);
		int percent = (int)(scaleBefore * 100.f + 0.5f);
		row(tr("graphics.render_scale"));
		const bool scaleChanged = ImGui::SliderInt("##render_scale", &percent, 50, 100, "%d%%");
		if (ImGui::IsItemActivated()) {
			g_pressedSlider = "graphics.render_scale";
			g_pressedValue = scaleBefore;
		}
		if (scaleChanged) {
			c.setNum("graphics.render_scale", percent / 100.f);
			c.save();
		}
		const int limits[] = { 0, 30, 60, 120 };
		const int limit = (int)c.num("graphics.fps_limit", 0.f);
		char current[32];
		if (limit > 0) snprintf(current, sizeof(current), "%d", limit);
		else snprintf(current, sizeof(current), "%s", tr("graphics.fps_unlimited"));
		row(tr("graphics.fps_limit"));
		if (ImGui::BeginCombo("##fps_limit", current)) {
			for (int l : limits) {
				char label[32];
				if (l > 0) snprintf(label, sizeof(label), "%d", l);
				else snprintf(label, sizeof(label), "%s", tr("graphics.fps_unlimited"));
				if (ImGui::Selectable(label, l == limit)) {
					c.setNum("graphics.fps_limit", (float)l);
					c.save();
				}
			}
			ImGui::EndCombo();
		}
		checkRow(tr("graphics.show_fps"), "graphics.show_fps", false);
		ImGui::EndTable();
	}
}

void applyAudioSettings(Audio& audio) {
	Config& c = Config::get();
	audio.setMuted(c.flag("audio.mute", false));
	audio.setMasterVolume(c.num("audio.volume", 1.f));
	audio.setMusicMuted(c.flag("audio.music_mute", false));
	audio.setMusicVolume(c.num("audio.music_volume", 1.f));
	audio.setSoundsMuted(c.flag("audio.sounds_mute", false));
	audio.setSoundsVolume(c.num("audio.sounds_volume", 1.f));
}

void Overlay::drawAudio() {
	bool changed = false;
	section(tr("audio.section"));
	if (beginRows("general")) {
		changed |= checkRow(tr("audio.mute"), "audio.mute", false);
		changed |= sliderRow(tr("audio.volume"), "audio.volume", 1.f, 0.f, 1.f, "%.2f");
		ImGui::EndTable();
	}
	section(tr("audio.section_music"));
	if (beginRows("music")) {
		changed |= checkRow(tr("audio.mute"), "audio.music_mute", false);
		changed |= sliderRow(tr("audio.volume"), "audio.music_volume", 1.f, 0.f, 1.f, "%.2f");
		ImGui::EndTable();
	}
	section(tr("audio.section_sounds"));
	if (beginRows("sounds")) {
		changed |= checkRow(tr("audio.mute"), "audio.sounds_mute", false);
		changed |= sliderRow(tr("audio.volume"), "audio.sounds_volume", 1.f, 0.f, 1.f, "%.2f");
		ImGui::EndTable();
	}
	if (changed && m_audio) applyAudioSettings(*m_audio);
}

void Overlay::drawImprovements() {
	section(tr("improvements.section"));
	if (beginRows("improvements")) {
		if (checkRow(tr("improvements.visible_sections"), "improvements.visible_sections", true)) Config::get().applyToEnvironment();
		ImGui::EndTable();
	}
}

void Overlay::drawAbout() {
	ImGui::Spacing();
	drawTitle(420.f * m_scale);
	ImGui::Spacing();
	const std::string version = format(tr("about.version"), REDBOUND_VERSION);
	centeredText(version.c_str(), true);
	centeredText(tr("about.by"));
	section(tr("about.section_community"));
	struct Link { const char* icon; const char* label; const char* url; };
	const Link links[] = {
		{ icon::discord, "Discord", "https://discord.gg/WXe65ENvwq" },
		{ icon::github, "GitHub", "https://github.com/alox-dev/redbound" },
		{ icon::youtube, "YouTube", "https://www.youtube.com/@aloxdev" },
		{ icon::mug, "Ko-fi", "https://ko-fi.com/aloxdev" },
	};
	const int perRow = m_compact && ImGui::GetContentRegionAvail().x < 520.f * m_scale ? 2 : 4;
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float bw = (ImGui::GetContentRegionAvail().x - spacing * (perRow - 1)) / perRow;
	for (int i = 0; i < 4; ++i) {
		if (i % perRow) ImGui::SameLine();
		const std::string label = m_icons ? std::string(links[i].icon) + "  " + links[i].label : std::string(links[i].label);
		if (ImGui::Button(label.c_str(), ImVec2(bw, 0))) SDL_OpenURL(links[i].url);
	}
	section(tr("about.section_original"));
	ImGui::PushTextWrapPos(0.f);
	ImGui::TextUnformatted(tr("about.original"));
	ImGui::TextDisabled("%s", tr("about.not_affiliated"));
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
	if (ImGui::CollapsingHeader(tr("about.libraries"))) {
		if (ImGui::BeginTable("libraries", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			const char* rows[][2] = {
				{ "KA3D (Jani Kajala)", "GPL v2" },
				{ "SDL2", "zlib" },
				{ "Dear ImGui", "MIT" },
				{ "dr_libs", "MIT-0" },
				{ "stb_image", "MIT" },
				{ "PVRTC (Imagination Technologies)", "MIT" },
				{ "SDL_GameControllerDB", "zlib" },
				{ "zlib", "zlib" },
				{ "ANGLE", "BSD" },
				{ "Font Awesome Free (icons)", "CC BY 4.0 / OFL" },
			};
			for (auto& r : rows) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(r[0]);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s", r[1]);
			}
			ImGui::EndTable();
		}
	}
	ImGui::Spacing();
	centeredText(tr("about.license"), true);
}

}
