#include "Citro3D.h"
#include "FileUtils.h"
#include "LaunchOptions.h"
#include "Paths.h"
#include "SceneSetup.h"
#include "SceneWorld.h"
#include "PassRecipe.h"
#include <hgr/Camera.h>
#include <hgr/DefaultResourceManager.h>
#include <hgr/Mesh.h>
#include <hgr/ParticleSystem.h>
#include <gr/impl/DIPrimitive.h>
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
#include <vector>

using namespace redbound;

static const int TOP_WIDTH = 400;
static const int TOP_HEIGHT = 240;
static const float TURN_SPEED = 1.8f;
static const float SPEEDS[] = { 120.f, 400.f, 1500.f };

static float stickAxis(int raw) {
	const float dead = 15.f, full = 150.f;
	const float a = std::fabs((float)raw);
	if (a <= dead) return 0.f;
	const float t = std::min(1.f, (a - dead) / (full - dead));
	return (raw < 0 ? -1.f : 1.f) * t * t;
}

struct FreeCamera {
	math::float3 position;
	float yaw = 0.f, pitch = 0.f;

	math::float3 forward() const { return math::float3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch)); }
	math::float3 right() const { return math::float3(cosf(yaw), 0.f, -sinf(yaw)); }

	void startFrom(hgr::Camera* camera) {
		const math::float3x4 tm = camera->worldTransform();
		position = tm.translation();
		math::float3 f(tm(0, 2), tm(1, 2), tm(2, 2));
		const float len = f.length();
		if (len > 0.f) f *= 1.f / len;
		yaw = atan2f(f.x, f.z);
		pitch = asinf(std::max(-1.f, std::min(1.f, f.y)));
	}

	void update(float dt, float speed, const circlePosition& move, const circlePosition& look, u32 held) {
		yaw += stickAxis(look.dx) * TURN_SPEED * dt;
		pitch += stickAxis(look.dy) * TURN_SPEED * 0.8f * dt;
		pitch = std::max(-1.5f, std::min(1.5f, pitch));
		const float step = speed * ((held & KEY_B) ? 4.f : 1.f) * dt;
		position += forward() * (stickAxis(move.dy) * step);
		position += right() * (stickAxis(move.dx) * step);
		if (held & KEY_R) position.y += step;
		if (held & KEY_L) position.y -= step;
	}

	void apply(hgr::Camera* camera) const {
		camera->setPosition(position);
		camera->lookAt(position + forward());
	}
};

enum MenuItem { MENU_CAMERA, MENU_VIEW, MENU_LIGHTMAP, MENU_PASSES, MENU_REFLECTION, MENU_CULLING, MENU_SWAP_UV, MENU_MESHES, MENU_SPEED, MENU_COUNT };

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

struct WaterSurface {
	hgr::Node* node;
	float height;
};

static bool isWaterMesh(hgr::Node* n) {
	if (n->classId() != hgr::Node::NODE_MESH) return false;
	hgr::Mesh* m = static_cast<hgr::Mesh*>(n);
	for (int i = 0; i < m->primitives(); ++i) {
		const gr::Citro3D_Shader* fx = dynamic_cast<gr::Citro3D_Shader*>(m->getPrimitive(i)->shader());
		if (fx && fx->effect() && fx->effect()->isWater()) return true;
	}
	return false;
}

static bool averageHeight(hgr::Node* n, float& height) {
	hgr::Mesh* m = static_cast<hgr::Mesh*>(n);
	const math::float3x4 w = n->worldTransform();
	double sum = 0;
	long count = 0;
	for (int i = 0; i < m->primitives(); ++i) {
		gr::DIPrimitive* p = dynamic_cast<gr::DIPrimitive*>(m->getPrimitive(i));
		if (!p || p->vertices() <= 0) continue;
		std::vector<math::float4> pos((size_t)p->vertices());
		p->lock(gr::ContextObject::LOCK_READ);
		p->getVertexPositions(0, &pos[0], (int)pos.size());
		p->unlock();
		for (const math::float4& v : pos) sum += w.transform(math::float3(v.x, v.y, v.z)).y;
		count += (long)pos.size();
	}
	if (!count) return false;
	height = (float)(sum / count);
	return true;
}

static std::vector<WaterSurface> findWater(hgr::Scene* scene) {
	std::vector<WaterSurface> out;
	for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
		WaterSurface w = { n, 0.f };
		if (isWaterMesh(n) && averageHeight(n, w.height)) out.push_back(w);
	}
	return out;
}

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
                                    const std::vector<WaterSurface>& water, gr::Texture* target, int clearColor) {
	ctx->reflectionTexture = 0;
	if (!target) return "no target";
	const math::float3x4 camTm = camera->worldTransform();
	const math::float3 camPos = camTm.translation();
	const WaterSurface* best = 0;
	float bestDistance = 1e30f;
	for (const WaterSurface& w : water) {
		if (!w.node->enabled()) continue;
		const math::float3 d = w.node->worldTransform().translation() - camPos;
		const float dd = d.x * d.x + d.y * d.y + d.z * d.z;
		if (dd < bestDistance) {
			bestDistance = dd;
			best = &w;
		}
	}
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

	gr::Citro3D_Context* ctx = new gr::Citro3D_Context(TOP_WIDTH, TOP_HEIGHT);
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
	P(hgr::Scene) scene;
	try {
		scene = new hgr::Scene(ctx, options.levelFile.c_str(), resources, options.textureDir().c_str(),
		                       options.shaderDir().c_str(), options.particleDir().c_str());
	} catch (lang::Throwable& e) {
		printf("\nlevel failed: %s\n", e.getMessage().format().c_str());
	}
	const float loadSeconds = (float)(svcGetSystemTick() - loadStart) / SYSCLOCK_ARM11;

	std::vector<hgr::Camera*> cameras;
	std::vector<WaterSurface> water;
	P(hgr::Camera) mirror;
	P(gr::Texture) reflectionTarget;
	std::unique_ptr<SceneWorld> world;
	std::vector<std::pair<P(hgr::ParticleSystem), float> > spawned;
	P(hgr::Camera) freeView;
	FreeCamera freeCamera;
	int cameraIndex = -1;

	std::vector<LibraryObject> libraryObjects;
	if (scene) {
		loadLibraryObjects(scene, ctx, resources, options, libraryObjects);
		hideHelperNodes(scene);
		cameras = collectCameras(scene);
		float fogStart = 0.f, fogEnd = 0.f, fogColor[3] = { 0.f, 0.f, 0.f };
		if (linearFog(scene, fogStart, fogEnd, fogColor)) {
			ctx->fogRange = math::float4(fogStart, fogEnd, 1.f / (fogEnd - fogStart), 0.f);
			ctx->fogColor = math::float4(fogColor[0], fogColor[1], fogColor[2], 1.f);
		}
		freeView = new hgr::Camera;
		freeView->setName("free_camera");
		freeView->linkTo(scene);
		if (cameras.empty()) {
			freeView->setHorizontalFov(1.2f);
			freeView->setFront(10.f);
			freeView->setBack(20000.f);
		} else {
			cameraIndex = 0;
			freeView->setHorizontalFov(cameras[0]->horizontalFov());
			freeView->setFront(cameras[0]->front());
			freeView->setBack(cameras[0]->back());
			freeCamera.startFrom(cameras[0]);
		}
		water = findWater(scene);
		clampSkyTextures(scene);
		reportMaterials(ctx, scene);
		world.reset(new SceneWorld(scene));
		for (LibraryObject& object : libraryObjects) {
			if (object.properties.find("a01") == std::string::npos) continue;
			world->registerNode(object.marker, object.visual);
			std::string props = object.properties;
			for (size_t at = props.find(",this"); at != std::string::npos; at = props.find(",this", at + 1)) {
				const char next = at + 5 < props.size() ? props[at + 5] : '\n';
				if (next == '\n' || next == '\r' || next == ',') props.replace(at + 1, 4, object.marker);
			}
			world->addTrigger(object.trigger, props);
		}
		if (world->triggerState("T_butterflyanim") >= 0) world->activateTrigger("T_butterflyanim");
		world->spawnParticles = [&](const std::string& file, hgr::Node* at) {
			try {
				hgr::ParticleSystem* source = resources->getParticleSystem((options.particleDir() + file).c_str(), options.textureDir().c_str(),
				                                                           options.shaderDir().c_str());
				if (!source) return;
				P(hgr::ParticleSystem) ps = new hgr::ParticleSystem(*source);
				ps->setTransform(at->worldTransform());
				ps->linkTo(scene);
				spawned.push_back(std::make_pair(ps, 8.f));
			} catch (lang::Throwable&) {
			}
		};
		if (!water.empty()) {
			mirror = new hgr::Camera;
			mirror->setName("water_mirror");
			mirror->linkTo(scene);
			reflectionTarget = ctx->createTexture(128, 256, gr::SurfaceFormat(gr::SurfaceFormat::SURFACE_A8R8G8B8), 0, 0);
		}
	}

	const unsigned clearColor = scene ? backgroundColor(scene) : 0xFF6688AAu;
	const bool reportWritten = ctx->writeReport(userDataDir() + "/probe-log.txt");
	char loadLine[64], sceneLine[64];
	snprintf(loadLine, sizeof(loadLine), "load %.1fs: tex %.1f fx %.1f prim %.1f", loadSeconds, (float)ctx->textureTicks / SYSCLOCK_ARM11,
	         (float)ctx->shaderTicks / SYSCLOCK_ARM11, (float)ctx->primitiveTicks / SYSCLOCK_ARM11);
	snprintf(sceneLine, sizeof(sceneLine), "%d prims, %d tex %uKB, %d lib, %d water", ctx->primitiveCount, ctx->texturesLoaded,
	         (unsigned)(ctx->textureBytes / 1024), (int)libraryObjects.size(), (int)water.size());
	if (!scene) {
		printf("\nlevel FAILED, START exits\n");
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
	static const char* const SPEED_NAMES[] = { "slow", "normal", "fast" };
	static const char* const PASS_NAMES[] = { "all", "opaque only", "blended only" };
	int menu = 0;
	int lightmapChoice = 0;
	int speedChoice = 1;
	int meshLimit = 0;
	bool reflectionEnabled = true;
	bool useFreeCamera = cameras.empty();
	const char* reflectionState = "off";
	KeyRepeat repeatUp, repeatDown, repeatLeft, repeatRight;
	ctx->lightmapMode = LIGHTMAP_MODES[lightmapChoice];
	u64 lastTick = svcGetSystemTick();
	float fpsAverage = 0.f;
	int frames = 0;

	while (scene && aptMainLoop()) {
		hidScanInput();
		const u32 down = hidKeysDown();
		const u32 held = hidKeysHeld();
		if (down & KEY_START) break;

		const u64 now = svcGetSystemTick();
		const float dt = std::min(0.1f, (float)(now - lastTick) / SYSCLOCK_ARM11);
		lastTick = now;
		ctx->time += dt;

		if (repeatUp.update(held & KEY_DUP, dt)) menu = (menu + MENU_COUNT - 1) % MENU_COUNT;
		if (repeatDown.update(held & KEY_DDOWN, dt)) menu = (menu + 1) % MENU_COUNT;
		const int left = repeatLeft.update(held & KEY_DLEFT, dt), right = repeatRight.update(held & KEY_DRIGHT, dt);
		const int change = right ? 1 : left ? -1 : 0;
		const int amount = std::max(left, right);
		if (down & KEY_Y) {
			useFreeCamera = !useFreeCamera;
			if (useFreeCamera && cameraIndex >= 0) freeCamera.startFrom(cameras[cameraIndex]);
		}
		if (change) {
			auto cycle = [&](int value, int count) { return (value + change + count) % count; };
			switch (menu) {
			case MENU_CAMERA:
				if (cameras.empty()) break;
				if (useFreeCamera) {
					useFreeCamera = false;
					cameraIndex = change > 0 ? 0 : (int)cameras.size() - 1;
				} else if ((change > 0 && cameraIndex == (int)cameras.size() - 1) || (change < 0 && cameraIndex == 0)) {
					useFreeCamera = true;
					freeCamera.startFrom(cameras[cameraIndex]);
				} else {
					cameraIndex += change;
				}
				break;
			case MENU_VIEW: ctx->debugMode = cycle(ctx->debugMode, 4); break;
			case MENU_LIGHTMAP:
				lightmapChoice = cycle(lightmapChoice, 4);
				ctx->lightmapMode = LIGHTMAP_MODES[lightmapChoice];
				break;
			case MENU_PASSES: ctx->passFilter = cycle(ctx->passFilter, 3); break;
			case MENU_REFLECTION: reflectionEnabled = !reflectionEnabled; break;
			case MENU_CULLING: ctx->invertCulling = !ctx->invertCulling; break;
			case MENU_SWAP_UV: ctx->swapUV = !ctx->swapUV; break;
			case MENU_MESHES: meshLimit = std::max(0, meshLimit + change * amount); break;
			case MENU_SPEED: speedChoice = std::max(0, std::min(2, speedChoice + change)); break;
			}
		}
		ctx->meshLimit = meshLimit;

		if (world) world->update(dt);
		int particleSystems = 0;
		for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene))
			if (n->classId() == hgr::Node::NODE_PARTICLESYSTEM) {
				static_cast<hgr::ParticleSystem*>(n)->update(dt);
				++particleSystems;
			}
		for (size_t i = 0; i < spawned.size();) {
			spawned[i].second -= dt;
			if (spawned[i].second <= 0.f) {
				spawned[i].first->unlink();
				spawned.erase(spawned.begin() + (long)i);
			} else {
				++i;
			}
		}

		circlePosition move, look;
		hidCircleRead(&move);
		hidCstickRead(&look);
		hgr::Camera* camera = useFreeCamera || cameraIndex < 0 ? (hgr::Camera*)freeView : cameras[cameraIndex];
		if (camera == freeView) {
			freeCamera.update(dt, SPEEDS[speedChoice], move, look, held);
			freeCamera.apply(freeView);
		}

		ctx->meshesDrawn = 0;
		for (int i = 0; i < 4; ++i) ctx->combinerUse[i] = 0;
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		ctx->setTarget(target, TOP_WIDTH, TOP_HEIGHT);
		ctx->reflectionTexture = 0;
		reflectionState = "off";
		if (reflectionEnabled && mirror)
			reflectionState = renderReflection(ctx, camera, mirror, water, reflectionTarget, (int)clearColor);
		ctx->clear((int)clearColor);
		camera->render(ctx);
		C3D_FrameEnd(0);

		fpsAverage += 1.f / std::max(0.0001f, dt);
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
			line(10, "reflection: %s  particles %d", reflectionState, particleSystems);
			line(12, "---------- debug menu ----------");
			for (int i = 0; i < MENU_COUNT; ++i) {
				char value[40] = "";
				switch (i) {
				case MENU_CAMERA:
					if (camera == freeView) snprintf(value, sizeof(value), "free");
					else snprintf(value, sizeof(value), "%d/%d %s", cameraIndex + 1, (int)cameras.size(), camera->name().c_str());
					break;
				case MENU_VIEW: snprintf(value, sizeof(value), "%s", VIEW_NAMES[ctx->debugMode]); break;
				case MENU_LIGHTMAP: snprintf(value, sizeof(value), "%s", LIGHTMAP_NAMES[lightmapChoice]); break;
				case MENU_PASSES: snprintf(value, sizeof(value), "%s", PASS_NAMES[ctx->passFilter]); break;
				case MENU_REFLECTION: snprintf(value, sizeof(value), "%s", reflectionEnabled ? "on" : "off"); break;
				case MENU_CULLING: snprintf(value, sizeof(value), "%s", ctx->invertCulling ? "inverted" : "normal"); break;
				case MENU_SWAP_UV: snprintf(value, sizeof(value), "%s", ctx->swapUV ? "on" : "off"); break;
				case MENU_MESHES:
					if (meshLimit == 0) snprintf(value, sizeof(value), "all (%d)", ctx->meshesDrawn);
					else snprintf(value, sizeof(value), "%d %s", meshLimit, ctx->isolatedMaterial);
					break;
				case MENU_SPEED: snprintf(value, sizeof(value), "%s", SPEED_NAMES[speedChoice]); break;
				}
				static const char* const LABELS[] = { "Camera", "View", "Lightmap", "Passes", "Reflection", "Culling", "Swap UV", "Meshes", "Speed" };
				line(13 + i, "%c %-11s %s", i == menu ? '>' : ' ', LABELS[i], value);
			}
			line(26, "Pad move, C-Stick look, L/R down/up");
			line(27, "hold B fast, Y free/level camera");
			line(28, "D-Pad up/down pick, left/right change");
			line(29, "START exit");
			fpsAverage = 0.f;
			frames = 0;
		}
	}

	gr::citro3dShuttingDown = true;
	world.reset();
	spawned.clear();
	ctx->reflectionTexture = 0;
	reflectionTarget = 0;
	mirror = 0;
	freeView = 0;
	libraryObjects.clear();
	scene = 0;
	resources = 0;
	cameras.clear();
	C3D_Fini();
	gfxExit();
	return 0;
}
