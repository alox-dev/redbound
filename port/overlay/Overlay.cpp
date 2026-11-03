#include "Overlay.h"
#include "Config.h"
#include "DebImport.h"
#include "Audio.h"
#include "AndroidBridge.h"
#include "I18n.h"
#include <dirent.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <sys/stat.h>

namespace redbound {

static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static std::string findFont() {
	if (const char* f = getenv("REDBOUND_FONT")) return f;
#ifdef __ANDROID__
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
#ifdef __ANDROID__
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
	else io.FontGlobalScale = std::max(1.f, m_scale * 1.5f);
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
	m_ready = true;
	Strings::get().load(Config::get().str("general.language", "auto"));
	printf("port menu: ready (Esc, Back/View/- or a three finger tap), %.0f dpi, font %.0f px%s%s, language %s\n", ddpi, fontPx,
	       m_compact ? ", compact" : "", font.empty() ? " (no system font)" : "", Strings::get().current().c_str());
	return true;
}

void Overlay::shutdown() {
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
	if (!m_open) return false;
	ImGui_ImplSDL2_ProcessEvent(&e);
	switch (e.type) {
	case SDL_QUIT: case SDL_WINDOWEVENT: return false;
	default: return true;
	}
}

void Overlay::draw() {
	if (!m_ready || !m_open) return;
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
		if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
			ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
		ImGui::EndChild();
	};
	auto tab = [](const char* key, const char* id) { return std::string(tr(key)) + "###" + id; };
	if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
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

void Overlay::drawLanguage() {
	Config& c = Config::get();
	const std::string selected = c.str("general.language", "auto");
	const auto languages = Strings::get().available();
	std::string preview = tr("game.language_auto");
	for (const auto& l : languages) if (l.first == selected) preview = l.second;
	if (ImGui::BeginCombo(tr("game.language"), preview.c_str())) {
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

void Overlay::drawGame() {
	const std::string dir = defaultGameDir();
	const bool ok = gameDataPresent(dir);
	if (m_importMode) {
		ImGui::TextWrapped("%s", tr("game.welcome"));
		ImGui::Spacing();
	}
	ImGui::TextWrapped(tr("game.data_dir"), dir.c_str());
	ImGui::TextColored(ok ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(1, 0.5f, 0.4f, 1), "%s", ok ? tr("game.data_ok") : tr("game.data_missing"));
	ImGui::Spacing();
	if (!m_importMode) ImGui::TextWrapped("%s", tr("game.notice"));
	const char* button = ok ? tr("game.import_another") : tr("game.choose_deb");
#ifdef __ANDROID__
	if (ImGui::Button(button, ImVec2(m_compact ? -1.f : 0.f, 0))) {
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
		ImGui::TextDisabled(tr("game.copy_by_usb"), external.c_str());
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
	if (ImGui::Button(button)) {
		const std::string deb = pickDeb();
		if (!deb.empty()) importFrom(deb);
		else m_status = tr("game.no_picker");
	}
#endif
	if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
	ImGui::Separator();
	drawLanguage();
}

void Overlay::drawControls() {
	Config& c = Config::get();
	const char* keys[] = { "auto", "classic", "modern" };
	const char* labels[] = { tr("controls.scheme_auto"), tr("controls.scheme_classic"), tr("controls.scheme_modern") };
	const std::string cur = c.str("controls.scheme", "auto");
	ImGui::TextUnformatted(tr("controls.scheme"));
	for (int i = 0; i < 3; ++i)
		if (ImGui::RadioButton(labels[i], cur == keys[i])) { c.set("controls.scheme", keys[i]); c.save(); }
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
	ImGui::TextWrapped("%s", tr("controls.scheme_help"));
	ImGui::PopStyleColor();
	ImGui::Separator();
	float sens = c.num("controls.camera_sensitivity", 1.f);
	if (ImGui::SliderFloat(tr("controls.camera_sensitivity"), &sens, 0.3f, 3.f, "%.1f")) { c.setNum("controls.camera_sensitivity", sens); c.save(); }
	bool inv = c.flag("controls.invert_y", false);
	if (ImGui::Checkbox(tr("controls.invert_y"), &inv)) { c.setFlag("controls.invert_y", inv); c.save(); }
	float dz = c.num("controls.deadzone", 0.22f);
	if (ImGui::SliderFloat(tr("controls.deadzone"), &dz, 0.05f, 0.5f, "%.2f")) { c.setNum("controls.deadzone", dz); c.save(); }
	ImGui::Separator();
	ImGui::TextUnformatted(tr("controls.connected"));
	int n = 0;
	for (int i = 0; i < SDL_NumJoysticks(); ++i)
		if (SDL_IsGameController(i)) { ImGui::BulletText("%s", SDL_GameControllerNameForIndex(i)); ++n; }
	if (!n) ImGui::TextDisabled("%s", tr("controls.none_connected"));
	ImGui::Separator();
	ImGui::TextUnformatted(tr("controls.mapping"));
	if (ImGui::BeginTable("mapping", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		const char* rows[][2] = {
			{ "controls.row_keyboard", "controls.row_keyboard_text" },
			{ "controls.row_mouse", "controls.row_mouse_text" },
			{ "controls.row_dpad", "controls.row_dpad_text" },
			{ "controls.row_sticks", "controls.row_sticks_text" },
			{ "controls.row_buttons", "controls.row_buttons_text" },
			{ "controls.row_pause", "controls.row_pause_text" },
			{ "controls.row_menu", "controls.row_menu_text" },
		};

		for (auto& r : rows) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(tr(r[0]));
			ImGui::TableSetColumnIndex(1);
			ImGui::TextWrapped("%s", tr(r[1]));
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	ImGui::TextUnformatted(tr("controls.touch"));
	const std::string ts = c.str("controls.touch", "gestures");
	if (ImGui::RadioButton(tr("controls.touch_gestures"), ts == "gestures")) { c.set("controls.touch", "gestures"); c.save(); }
	if (ImGui::RadioButton(tr("controls.touch_accelerometer"), ts == "accelerometer")) { c.set("controls.touch", "accelerometer"); c.save(); }
	if (ts == "gestures") {
		bool swapped = c.flag("controls.touch_swap_sides", false);
		if (ImGui::Checkbox(tr("controls.touch_swap_sides"), &swapped)) {
			c.setFlag("controls.touch_swap_sides", swapped);
			c.save();
		}
	}
	if (ts == "accelerometer") {
		float ts2 = c.num("controls.tilt_sensitivity", 1.f);
		if (ImGui::SliderFloat(tr("controls.tilt_sensitivity"), &ts2, 0.4f, 2.5f, "%.1f")) { c.setNum("controls.tilt_sensitivity", ts2); c.save(); }
		bool ix = c.flag("controls.tilt_invert_x", false), iy = c.flag("controls.tilt_invert_y", false);
		if (ImGui::Checkbox(tr("controls.tilt_invert_x"), &ix)) { c.setFlag("controls.tilt_invert_x", ix); c.save(); }
		if (ImGui::Checkbox(tr("controls.tilt_invert_y"), &iy)) { c.setFlag("controls.tilt_invert_y", iy); c.save(); }
		const std::string ac = c.str("controls.tilt_camera", "follow");
		if (ImGui::RadioButton(tr("controls.tilt_camera_follow"), ac == "follow")) { c.set("controls.tilt_camera", "follow"); c.save(); }
		if (ImGui::RadioButton(tr("controls.tilt_camera_fixed"), ac == "fixed")) { c.set("controls.tilt_camera", "fixed"); c.save(); }
		if (ImGui::Button(tr("controls.calibrate")) && onCalibrate) { onCalibrate(); m_status = tr("controls.calibrated"); }
	}
	bool vib = c.flag("controls.vibration", true);
	if (ImGui::Checkbox(tr("controls.vibration"), &vib)) { c.setFlag("controls.vibration", vib); c.save(); }
}

void Overlay::drawGraphics() {
	Config& c = Config::get();
	bool vsync = c.flag("graphics.vsync", true);
	if (ImGui::Checkbox(tr("graphics.vsync"), &vsync)) {
		c.setFlag("graphics.vsync", vsync);
		SDL_GL_SetSwapInterval(vsync ? 1 : 0);
		c.save();
	}
	bool full = (SDL_GetWindowFlags(m_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
	if (ImGui::Checkbox(tr("graphics.fullscreen"), &full)) {
		SDL_SetWindowFullscreen(m_win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		c.setFlag("graphics.fullscreen", full);
		c.save();
	}
	int w, h;
	SDL_GL_GetDrawableSize(m_win, &w, &h);
	ImGui::Text(tr("graphics.resolution"), w, h);
}

void Overlay::drawAudio() {
	Config& c = Config::get();
	bool muted = c.flag("audio.mute", false);
	if (ImGui::Checkbox(tr("audio.mute"), &muted)) {
		c.setFlag("audio.mute", muted);
		if (m_audio) m_audio->setMuted(muted);
		c.save();
	}
	float vol = c.num("audio.volume", 1.f);
	if (ImGui::SliderFloat(tr("audio.volume"), &vol, 0.f, 1.f, "%.2f")) {
		c.setNum("audio.volume", vol);
		if (m_audio) m_audio->setMasterVolume(vol);
		c.save();
	}
}

void Overlay::drawImprovements() {
	Config& c = Config::get();
	ImGui::TextWrapped("%s", tr("improvements.intro"));
	ImGui::Spacing();
	struct Item { const char* key; const char* label; const char* help; };
	const Item items[] = {
		{ "improvements.visible_sections", "improvements.visible_sections", "improvements.visible_sections_help" },
	};
	for (const Item& it : items) {
		bool v = c.flag(it.key, true);
		if (ImGui::Checkbox(tr(it.label), &v)) {
			c.setFlag(it.key, v);
			c.save();
			c.applyToEnvironment();
		}
		ImGui::TextDisabled("%s", tr(it.help));
	}
}

void Overlay::drawAbout() {
	ImGui::Text("Redbound %s", REDBOUND_VERSION);
	ImGui::TextWrapped("%s", tr("about.description"));
	ImGui::Spacing();
	ImGui::TextWrapped("%s", tr("about.developed"));
	ImGui::TextWrapped("%s", tr("about.original"));
	ImGui::TextWrapped("%s", tr("about.not_affiliated"));
	ImGui::Separator();
	ImGui::TextUnformatted(tr("about.libraries"));
	if (ImGui::BeginTable("libraries", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		const char* rows[][2] = {
			{ "KA3D (Jani Kajala)", "GPL v2" },
			{ "SDL2", "zlib" },
			{ "Dear ImGui", "MIT" },
			{ "dr_libs (dr_wav, dr_mp3)", "Public domain / MIT-0" },
			{ "stb_image", "Public domain / MIT" },
			{ "PVRTC decompressor (Imagination Technologies)", "MIT" },
			{ "SDL_GameControllerDB", "zlib" },
			{ "zlib", "zlib" },
		};
		for (auto& r : rows) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(r[0]);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(r[1]);
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	ImGui::TextWrapped("%s", tr("about.license"));
	ImGui::TextWrapped("%s", tr("about.source"));
	ImGui::TextWrapped("%s", tr("about.support"));
}

}
