#include "BallController.h"
#include "Citro3D.h"
#include "FileUtils.h"
#include "LaunchOptions.h"
#include "LevelCore.h"
#include "Paths.h"
#include "SceneSetup.h"
#include "WaterSurfaces.h"
#include "PassRecipe.h"
#include <hgr/Camera.h>
#include <hgr/Dummy.h>
#include <hgr/DefaultResourceManager.h>
#include <hgr/Mesh.h>
#include <hgr/Scene.h>
#include <hgr/SceneInputStream.h>
#include <io/FileInputStream.h>
#include <lang/Throwable.h>
#include <math/float3.h>
#include <3ds.h>
#include <citro3d.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <set>
#include <string>

using namespace redbound;

static const int TOP_WIDTH = 400;
static const int TOP_HEIGHT = 240;
static const float FLY_TURN_SPEED = 1.8f;

static float stickCurve(int raw) {
	const float dead = 15.f, full = 150.f;
	const float a = std::fabs((float)raw);
	if (a <= dead) return 0.f;
	const float t = std::min(1.f, (a - dead) / (full - dead));
	return (raw < 0 ? -1.f : 1.f) * t * t;
}

static float stickLinear(int raw) {
	const float dead = 20.f, full = 150.f;
	const float a = std::fabs((float)raw);
	if (a <= dead) return 0.f;
	return (raw < 0 ? -1.f : 1.f) * std::min(1.f, (a - dead) / (full - dead));
}

static LevelInput readInput(const LevelCore& core, float dt, u32 held) {
	circlePosition move, look;
	hidCircleRead(&move);
	hidCstickRead(&look);
	LevelInput input;
	const bool jump = (held & (KEY_A | KEY_B)) != 0;
	if (core.player() && !core.freeFly()) {
		PlayerCommand& cmd = input.command;
		const float lx = stickLinear(move.dx), ly = stickLinear(move.dy);
		const float rx = stickLinear(look.dx), ry = -stickLinear(look.dy);
		cmd.modern = true;
		cmd.moveX = cmd.turn = lx;
		cmd.moveY = cmd.forward = ly;
		cmd.cameraYaw = rx * 2.6f * dt;
		cmd.cameraPitch = ry * 1.8f * dt;
		cmd.jump = jump;
		input.tap = jump;
	} else {
		FreeFlyInput& fly = input.fly;
		fly.yaw = stickCurve(look.dx) * FLY_TURN_SPEED * dt;
		fly.pitch = stickCurve(look.dy) * FLY_TURN_SPEED * 0.8f * dt;
		fly.forward = stickCurve(move.dy);
		fly.right = stickCurve(move.dx);
		fly.up = ((held & KEY_R) ? 1.f : 0.f) - ((held & KEY_L) ? 1.f : 0.f);
		fly.fast = (held & KEY_B) != 0;
	}
	return input;
}

enum MenuItem { MENU_CAMERA, MENU_LEVEL_CAMERA, MENU_SPAWN_FX, MENU_VIEW, MENU_LIGHTMAP, MENU_PASSES, MENU_REFLECTION, MENU_WATER, MENU_SPECULAR, MENU_MESHES, MENU_COUNT };

struct KeyRepeat {
	float held = 0.f, next = 0.f;

	int update(bool pressed, float dt) {
		if (!pressed) {
			held = next = 0.f;
			return 0;
		}
		const bool first = held == 0.f;
		held += dt;
		if (first) {
			next = 0.35f;
			return 1;
		}
		if (held < next) return 0;
		next = held + 0.07f;
		return held > 1.5f ? 10 : 1;
	}
};

static void line(int row, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void line(int row, const char* fmt, ...) {
	char text[64];
	va_list args;
	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	printf("\x1b[%d;1H%-39.39s", row, text);
}

static std::string findLevel(const std::string& dataDir, int index) {
	char name[64];
	snprintf(name, sizeof(name), "/bundles/original/level%02d.hgr", index);
	return dataDir + name;
}

static u32 freeLinear() { return (u32)linearSpaceFree(); }

static float tickMs(uint64_t ticks, int frames) { return (float)ticks * 1000.f / SYSCLOCK_ARM11 / (float)std::max(1, frames); }

struct FrameProfile {
	uint64_t logic = 0, wait = 0, reflection = 0, scene = 0, submit = 0;
};

static void clampSkyTextures(hgr::Scene* scene) {
	for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
		if (n->classId() != hgr::Node::NODE_MESH) continue;
		hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
		bool sky = false;
		for (int i = 0; i < mesh->primitives(); ++i) {
			gr::Shader* fx = mesh->getPrimitive(i)->shader();
			if (fx && materialIsSky(fx->name().c_str())) sky = true;
		}
		if (!sky) continue;
		for (int i = 0; i < mesh->primitives(); ++i)
			if (gr::Citro3D_Shader* fx = dynamic_cast<gr::Citro3D_Shader*>(mesh->getPrimitive(i)->shader()))
				if (gr::Citro3D_Texture* t = dynamic_cast<gr::Citro3D_Texture*>(fx->getTexture("BASEMAP"))) t->clampToEdge();
	}
}

static std::string textureNote(gr::Citro3D_Shader* fx, const char* sampler) {
	gr::Citro3D_Texture* t = dynamic_cast<gr::Citro3D_Texture*>(fx->getTexture(sampler));
	if (!t) return "-";
	char note[96];
	snprintf(note, sizeof(note), "%s %dx%d %s", t->name().c_str(), t->width(), t->height(), t->hasAlpha() ? "rgba8" : "rgb565");
	return note;
}

static void reportMaterials(gr::Citro3D_Context* ctx, hgr::Scene* scene) {
	std::set<std::string> seen;
	for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
		if (n->classId() != hgr::Node::NODE_MESH) continue;
		hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
		for (int i = 0; i < mesh->primitives(); ++i) {
			gr::Citro3D_Shader* fx = dynamic_cast<gr::Citro3D_Shader*>(mesh->getPrimitive(i)->shader());
			if (!fx || !fx->effect()) continue;
			const std::string key = std::string(fx->name().c_str()) + "|" + fx->effect()->name;
			if (!seen.insert(key).second) continue;
			const gr::Citro3D_Effect* e = fx->effect();
			const size_t slash = e->name.find_last_of('/');
			char note[400];
			snprintf(note, sizeof(note), "mat '%s' fx %s lit %d lm %d sort %d node %s base %s simap %s", fx->name().c_str(),
			         e->name.substr(slash == std::string::npos ? 0 : slash + 1).c_str(), (int)e->lit, (int)e->lightmap, (int)fx->sort(),
			         n->name().c_str(), textureNote(fx, "BASEMAP").c_str(), textureNote(fx, "SIMAP").c_str());
			ctx->report(note);
		}
	}
}

static const char* renderReflection(gr::Citro3D_Context* ctx, hgr::Camera* camera, hgr::Camera* mirror,
                                    const WaterSurfaces& water, gr::Texture* target, int clearColor) {
	ctx->reflectionTexture = 0;
	if (!target) return "no target";
	const math::float3x4 camTm = camera->worldTransform();
	const math::float3 camPos = camTm.translation();
	const WaterSurface* best = water.nearest(camPos);
	if (!best) return "no water";
	if (camPos.y < best->height) return "below water";

	math::float3x4 reflect(1.f);
	reflect(1, 1) = -1.f;
	reflect(1, 3) = 2.f * best->height;
	mirror->setHorizontalFov(camera->horizontalFov());
	mirror->setFront(camera->front());
	mirror->setBack(camera->back());
	mirror->setTransform(reflect * camTm);

	const int meshLimit = ctx->meshLimit;
	ctx->meshLimit = 0;
	ctx->setRenderTarget(target);
	ctx->clear(clearColor);
	ctx->forcedTechnique = "Reflected";
	ctx->mirrored = true;
	mirror->render(ctx);
	ctx->mirrored = false;
	ctx->forcedTechnique.clear();
	ctx->setRenderTarget(0);
	ctx->meshLimit = meshLimit;
	ctx->meshesDrawn = 0;
	for (int i = 0; i < 4; ++i) ctx->combinerUse[i] = 0;
	ctx->reflectionTexture = target;
	return best->node->name().c_str();
}

int main() {
	gfxInitDefault();
	PrintConsole* console = consoleInit(GFX_BOTTOM, 0);
	(void)console;

	bool isNew3DS = false;
	APT_CheckNew3DS(&isNew3DS);
	if (isNew3DS) osSetSpeedupEnable(true);

	printf("Redbound 3DS probe\n");
	printf("model: %s\n", isNew3DS ? "New 3DS" : "Old 3DS");

	const std::string dataDir = userDataDir() + "/data";
	if (!isDirectory(dataDir)) {
		printf("\nmissing game data in\n%s\n\nimport the .deb on a computer\nand copy the data folder there.\n", dataDir.c_str());
		while (aptMainLoop()) {
			hidScanInput();
			if (hidKeysDown() & KEY_START) break;
			gspWaitForVBlank();
		}
		gfxExit();
		return 0;
	}

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
	C3D_RenderTarget* target = C3D_RenderTargetCreate(TOP_HEIGHT, TOP_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
	                          GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
	                          GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

	LaunchOptions options;
	options.dataDir = dataDir;
	options.levelFile = findLevel(dataDir, 1);

	options.play = true;

	P(gr::Citro3D_Context) ctx = new gr::Citro3D_Context(TOP_WIDTH, TOP_HEIGHT);
	ctx->setMaxTextureSize(isNew3DS ? 256 : 128);
	ctx->invertCulling = true;
	ctx->flipTextures = true;
	const std::string cacheDir = userDataDir() + "/cache";
	makeDirectories(cacheDir);
	if (isDirectory(cacheDir)) ctx->setCacheDir(cacheDir);
	ctx->setShaderDir(options.shaderDir());
	ctx->setTextureDir(options.textureDir());
	try {
		io::FileInputStream in(options.levelFile.c_str());
		hgr::SceneInputStream header(&in);
		ctx->setPlatformId((int)header.platform());
	} catch (lang::Throwable&) {
	}

	const u64 loadStart = svcGetSystemTick();
	P(hgr::DefaultResourceManager) resources = new hgr::DefaultResourceManager(ctx);
	std::unique_ptr<LevelCore> core(new LevelCore(ctx, resources, options));
	std::string failure;
	std::vector<std::string> particleNames;
	bool loaded = false;
	try {
		core->loadScene();
		core->setupWorld();
		loaded = true;
		particleNames = core->preloadParticles();
	} catch (lang::Throwable& e) {
		failure = e.getMessage().format().c_str();
	}
	const float levelSeconds = (float)(svcGetSystemTick() - loadStart) / SYSCLOCK_ARM11;
	std::string playerFailure;
	if (loaded) {
		try {
			core->setupPlayer();
		} catch (lang::Throwable& e) {
			playerFailure = e.getMessage().format().c_str();
		}
	}
	const float loadSeconds = (float)(svcGetSystemTick() - loadStart) / SYSCLOCK_ARM11;

	std::unique_ptr<WaterSurfaces> water;
	P(hgr::Camera) mirror;
	P(gr::Texture) reflectionTarget;
	std::vector<P(gr::Texture)> waterFrames;
	hgr::Scene* scene = loaded ? core->scene() : 0;

	if (scene) {
		const LevelFog& fog = core->fog();
		if (fog.enabled) {
			ctx->fogRange = math::float4(fog.start, fog.end, 1.f / (fog.end - fog.start), 0.f);
			ctx->fogColor = math::float4(fog.color[0], fog.color[1], fog.color[2], 1.f);
		}
		water.reset(new WaterSurfaces(scene, [](gr::Shader* shader) {
			const gr::Citro3D_Shader* fx = dynamic_cast<gr::Citro3D_Shader*>(shader);
			return fx && fx->effect() && fx->effect()->isWater();
		}));
		if (BallController* player = core->player()) {
			WaterSurfaces* surfaces = water.get();
			player->setWaterQuery([surfaces](float x, float z, float& height) { return surfaces->surfaceAt(x, z, height); });
		}
		core->setupCamera();
		clampSkyTextures(scene);
		reportMaterials(ctx, scene);
		if (!water->empty()) {
			mirror = new hgr::Camera;
			mirror->setName("water_mirror");
			mirror->linkTo(scene);
			reflectionTarget = ctx->createTexture(128, 256, gr::SurfaceFormat(gr::SurfaceFormat::SURFACE_A8R8G8B8), 0, 0);
			for (int i = 1; i <= 30; ++i) {
				char name[32];
				snprintf(name, sizeof(name), "water%02d.pvr", i);
				const int failed = ctx->texturesFailed;
				P(gr::Texture) frame = ctx->createTexture(name);
				if (ctx->texturesFailed != failed) break;
				waterFrames.push_back(frame);
			}
		}
	}

	const unsigned clearColor = scene ? core->clearColor() : 0xFF6688AAu;
	if (!playerFailure.empty()) ctx->report("ball: " + playerFailure);
	const bool reportWritten = ctx->writeReport(userDataDir() + "/probe-log.txt");
	char loadLine[64], sceneLine[64];
	snprintf(loadLine, sizeof(loadLine), "load %.1fs (level %.1f) tex %.1f prim %.1f", loadSeconds, levelSeconds,
	         (float)ctx->textureTicks / SYSCLOCK_ARM11, (float)ctx->primitiveTicks / SYSCLOCK_ARM11);
	snprintf(sceneLine, sizeof(sceneLine), "%d prims, %d tex %uKB, %d water, ball %s", ctx->primitiveCount, ctx->texturesLoaded,
	         (unsigned)(ctx->textureBytes / 1024), water ? (int)water->all().size() : 0, core->player() ? "ok" : "none");
	if (!scene) {
		printf("\nlevel FAILED: %s\nSTART exits\n", failure.c_str());
		while (aptMainLoop()) {
			hidScanInput();
			if (hidKeysDown() & KEY_START) break;
			gspWaitForVBlank();
		}
	}
	consoleClear();

	static const int LIGHTMAP_MODES[] = { 3, 1, 0, 2 };
	static const char* const LIGHTMAP_NAMES[] = { "exact (x2)", "add", "mul2", "off" };
	static const char* const VIEW_NAMES[] = { "normal", "untextured", "no lighting", "lightmap only" };
	static const char* const PASS_NAMES[] = { "all", "opaque only", "blended only" };
	int menu = 0;
	int fxChoice = 0;
	P(hgr::Dummy) fxAnchor = new hgr::Dummy;
	fxAnchor->setName("debug_fx");
	if (scene) fxAnchor->linkTo(scene);
	int lightmapChoice = 0;
	int meshLimit = 0;
	bool reflectionEnabled = true;
	const char* reflectionState = "off";
	KeyRepeat repeatUp, repeatDown, repeatLeft, repeatRight;
	ctx->lightmapMode = LIGHTMAP_MODES[lightmapChoice];
	u64 lastTick = svcGetSystemTick();
	FrameProfile profile;
	float fpsAverage = 0.f;
	int frames = 0;

	while (scene && aptMainLoop()) {
		hidScanInput();
		const u32 down = hidKeysDown();
		const u32 held = hidKeysHeld();
		if (down & KEY_START) break;

		const u64 now = svcGetSystemTick();
		const float realDt = std::min(0.1f, (float)(now - lastTick) / SYSCLOCK_ARM11);
		lastTick = now;
		ctx->time += realDt;

		if (repeatUp.update(held & KEY_DUP, realDt)) menu = (menu + MENU_COUNT - 1) % MENU_COUNT;
		if (repeatDown.update(held & KEY_DDOWN, realDt)) menu = (menu + 1) % MENU_COUNT;
		const int left = repeatLeft.update(held & KEY_DLEFT, realDt), right = repeatRight.update(held & KEY_DRIGHT, realDt);
		const int change = right ? 1 : left ? -1 : 0;
		const int amount = std::max(left, right);
		if (down & KEY_Y) core->toggleFreeFly();
		if (down & KEY_X) core->respawn();
		if ((down & KEY_SELECT) && !particleNames.empty()) {
			const math::float3x4 view = core->activeCamera()->worldTransform();
			math::float3x4 at(1.f);
			at.setTranslation(view.translation() + view.getColumn(2) * 400.f);
			fxAnchor->setTransform(at);
			core->spawnParticles(particleNames[(size_t)fxChoice], fxAnchor);
		}
		if (change) {
			auto cycle = [&](int value, int count) { return (value + change + count) % count; };
			switch (menu) {
			case MENU_CAMERA: core->toggleFreeFly(); break;
			case MENU_LEVEL_CAMERA: {
				const int steps = change > 0 ? 1 : (int)core->cameras().size();
				for (int i = 0; i < steps; ++i) core->cycleDevCamera();
				break;
			}
			case MENU_SPAWN_FX:
				if (!particleNames.empty()) fxChoice = cycle(fxChoice, (int)particleNames.size());
				break;
			case MENU_VIEW: ctx->debugMode = cycle(ctx->debugMode, 4); break;
			case MENU_LIGHTMAP:
				lightmapChoice = cycle(lightmapChoice, 4);
				ctx->lightmapMode = LIGHTMAP_MODES[lightmapChoice];
				break;
			case MENU_PASSES: ctx->passFilter = cycle(ctx->passFilter, 3); break;
			case MENU_REFLECTION: reflectionEnabled = !reflectionEnabled; break;
			case MENU_WATER: ctx->waterRipples = !ctx->waterRipples; break;
			case MENU_SPECULAR: ctx->specularEnabled = !ctx->specularEnabled; break;
			case MENU_MESHES: meshLimit = std::max(0, meshLimit + change * amount); break;
			}
		}
		ctx->meshLimit = meshLimit;

		const u64 logicStart = svcGetSystemTick();
		core->update(realDt, realDt, readInput(*core, realDt, held), false);
		ctx->waterNormal = waterFrames.empty() ? 0 : waterFrames[(size_t)(fmodf(ctx->time, 2.f) * 15.f) % waterFrames.size()];
		hgr::Camera* camera = core->activeCamera();
		profile.logic += svcGetSystemTick() - logicStart;

		ctx->meshesDrawn = 0;
		for (int i = 0; i < 4; ++i) ctx->combinerUse[i] = 0;
		const u64 waitStart = svcGetSystemTick();
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		const u64 reflectionStart = svcGetSystemTick();
		ctx->setTarget(target, TOP_WIDTH, TOP_HEIGHT);
		ctx->reflectionTexture = 0;
		reflectionState = "off";
		if (reflectionEnabled && mirror)
			reflectionState = renderReflection(ctx, camera, mirror, *water, reflectionTarget, (int)clearColor);
		const u64 sceneStart = svcGetSystemTick();
		ctx->clear((int)clearColor);
		camera->render(ctx);
		const u64 submitStart = svcGetSystemTick();
		C3D_FrameEnd(0);
		const u64 frameEnd = svcGetSystemTick();
		profile.wait += reflectionStart - waitStart;
		profile.reflection += sceneStart - reflectionStart;
		profile.scene += submitStart - sceneStart;
		profile.submit += frameEnd - submitStart;

		fpsAverage += 1.f / std::max(0.0001f, realDt);
		if (++frames >= 15) {
			const math::float3 pos = camera->worldTransform().translation();
			line(1, "Redbound 3DS probe (%s)", isNew3DS ? "New 3DS" : "Old 3DS");
			line(2, "%s", loadLine);
			line(3, "%s", sceneLine);
			line(4, "log %s", reportWritten ? "sdmc:/3ds/redbound/probe-log.txt" : "NOT written");
			line(6, "fps %.1f  cpu %.1f ms  gpu %.1f ms", fpsAverage / frames, C3D_GetProcessingTime(), C3D_GetDrawingTime());
			line(7, "linear %u KB  vram %u KB", (unsigned)(freeLinear() / 1024), (unsigned)(vramSpaceFree() / 1024));
			line(8, "draws vtx %d tex %d lm %d water %d", ctx->combinerUse[0], ctx->combinerUse[1], ctx->combinerUse[2], ctx->combinerUse[3]);
			line(9, "pos %.0f %.0f %.0f", pos.x, pos.y, pos.z);
			line(10, "reflection: %s", reflectionState);
			line(12, "---------- debug menu ----------");
			for (int i = 0; i < MENU_COUNT; ++i) {
				char value[40] = "";
				switch (i) {
				case MENU_CAMERA: snprintf(value, sizeof(value), "%s", core->freeFly() || !core->player() ? "free" : "game"); break;
				case MENU_LEVEL_CAMERA:
					if (core->devCamera() < 0) snprintf(value, sizeof(value), "none");
					else snprintf(value, sizeof(value), "%d/%d %s", core->devCamera() + 1, (int)core->cameras().size(),
					              core->cameras()[core->devCamera()]->name().c_str());
					break;
				case MENU_SPAWN_FX:
					snprintf(value, sizeof(value), "%s (SELECT)", particleNames.empty() ? "none" : particleNames[(size_t)fxChoice].c_str());
					break;
				case MENU_VIEW: snprintf(value, sizeof(value), "%s", VIEW_NAMES[ctx->debugMode]); break;
				case MENU_LIGHTMAP: snprintf(value, sizeof(value), "%s", LIGHTMAP_NAMES[lightmapChoice]); break;
				case MENU_PASSES: snprintf(value, sizeof(value), "%s", PASS_NAMES[ctx->passFilter]); break;
				case MENU_REFLECTION: snprintf(value, sizeof(value), "%s", reflectionEnabled ? "on" : "off"); break;
				case MENU_WATER: snprintf(value, sizeof(value), "%s (%d frames)", ctx->waterRipples ? "ripples" : "still", (int)waterFrames.size()); break;
				case MENU_SPECULAR: snprintf(value, sizeof(value), "%s", ctx->specularEnabled ? "on" : "off"); break;
				case MENU_MESHES:
					if (meshLimit == 0) snprintf(value, sizeof(value), "all (%d)", ctx->meshesDrawn);
					else snprintf(value, sizeof(value), "%d %s", meshLimit, ctx->isolatedMaterial);
					break;
				}
				static const char* const LABELS[] = { "Camera", "Level cam", "Spawn fx", "View", "Lightmap", "Passes", "Reflection", "Water", "Specular", "Meshes" };
				line(13 + i, "%c %-11s %s", i == menu ? '>' : ' ', LABELS[i], value);
			}
			line(23, "logic %.1f refl %.1f scene %.1f end %.1f", tickMs(profile.logic, frames), tickMs(profile.reflection, frames),
			     tickMs(profile.scene, frames), tickMs(profile.submit, frames));
			line(24, "pass %.1f upload %.1f sort %.1f draw %.1f", tickMs(ctx->passTicks, frames), tickMs(ctx->uploadTicks, frames),
			     tickMs(ctx->sortTicks, frames), tickMs(ctx->drawTicks, frames));
			line(25, "wait gpu %.1f ms, %d passes/frame", tickMs(profile.wait, frames), ctx->passCount / std::max(1, frames));
			line(26, "Pad move, C-Stick camera, A/B jump");
			line(27, "Y free camera, X respawn, L/R fly");
			line(28, "D-Pad up/down pick, left/right change");
			line(29, "START exit");
			profile = FrameProfile();
			ctx->passTicks = ctx->uploadTicks = ctx->sortTicks = ctx->drawTicks = 0;
			ctx->passCount = 0;
			fpsAverage = 0.f;
			frames = 0;
		}
	}

	gr::citro3dShuttingDown = true;
	ctx->waterNormal = 0;
	ctx->reflectionTexture = 0;
	waterFrames.clear();
	reflectionTarget = 0;
	mirror = 0;
	water.reset();
	fxAnchor = 0;
	core.reset();
	resources = 0;
	C3D_Fini();
	gfxExit();
	return 0;
}
