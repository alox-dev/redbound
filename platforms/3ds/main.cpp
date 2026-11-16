#include "Citro3D.h"
#include "FileUtils.h"
#include "LaunchOptions.h"
#include "Paths.h"
#include "SceneSetup.h"
#include <hgr/Camera.h>
#include <hgr/DefaultResourceManager.h>
#include <hgr/Mesh.h>
#include <gr/impl/DIPrimitive.h>
#include <hgr/Scene.h>
#include <hgr/SceneInputStream.h>
#include <io/FileInputStream.h>
#include <lang/Throwable.h>
#include <math/float3.h>
#include <3ds.h>
#include <citro3d.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace redbound;

static const int TOP_WIDTH = 400;
static const int TOP_HEIGHT = 240;
static const float MOVE_SPEED = 400.f;
static const float TURN_SPEED = 2.f;

struct FreeCamera {
	math::float3 position;
	float yaw = 0.f, pitch = 0.f;

	math::float3x4 transform() const {
		const math::float3x3 rotation = math::float3x3(math::float3(0, 1, 0), yaw) * math::float3x3(math::float3(1, 0, 0), pitch);
		return math::float3x4(rotation, position);
	}

	void update(float dt, const circlePosition& move, const circlePosition& look) {
		const float dead = 20.f;
		if (std::fabs((float)look.dx) > dead) yaw -= (float)look.dx / 156.f * TURN_SPEED * dt;
		if (std::fabs((float)look.dy) > dead) pitch += (float)look.dy / 156.f * TURN_SPEED * dt;
		pitch = std::max(-1.5f, std::min(1.5f, pitch));
		const math::float3x3 rotation = math::float3x3(math::float3(0, 1, 0), yaw) * math::float3x3(math::float3(1, 0, 0), pitch);
		math::float3 delta(0, 0, 0);
		if (std::fabs((float)move.dx) > dead) delta += rotation.getColumn(0) * ((float)move.dx / 156.f);
		if (std::fabs((float)move.dy) > dead) delta -= rotation.getColumn(2) * ((float)move.dy / 156.f);
		const u32 held = hidKeysHeld();
		if (held & KEY_X) delta += math::float3(0, 1, 0);
		if (held & KEY_B) delta -= math::float3(0, 1, 0);
		position += delta * (MOVE_SPEED * dt);
	}
};

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
	ctx->lightmapMode = 1;
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

	const u32 linearBefore = freeLinear();
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
	FreeCamera freeCamera;
	int cameraIndex = 0;
	bool useFreeCamera = false;
	P(hgr::Camera) camera;

	if (scene) {
		hideHelperNodes(scene);
		cameras = collectCameras(scene);
		float fogStart = 0.f, fogEnd = 0.f, fogColor[3] = { 0.f, 0.f, 0.f };
		if (linearFog(scene, fogStart, fogEnd, fogColor)) {
			ctx->fogRange = math::float4(fogStart, fogEnd, 1.f / (fogEnd - fogStart), 0.f);
			ctx->fogColor = math::float4(fogColor[0], fogColor[1], fogColor[2], 1.f);
		}
		camera = cameras.empty() ? 0 : cameras[0];
		if (!camera) {
			camera = new hgr::Camera;
			camera->linkTo(scene);
			useFreeCamera = true;
		}
		if (cameras.empty()) {
			camera->setHorizontalFov(1.2f);
			camera->setFront(10.f);
			camera->setBack(20000.f);
		}
		freeCamera.position = camera->position();
		water = findWater(scene);
		if (!water.empty()) {
			mirror = new hgr::Camera;
			mirror->setName("water_mirror");
			mirror->linkTo(scene);
			reflectionTarget = ctx->createTexture(128, 256, gr::SurfaceFormat(gr::SurfaceFormat::SURFACE_A8R8G8B8), 0, 0);
		}
	}

	const unsigned clearColor = scene ? backgroundColor(scene) : 0xFF6688AAu;
	printf("level: %s\n", scene ? "loaded" : "FAILED");
	printf("load: %.1f s = tex %.1f + fx %.1f + prim %.1f\n", loadSeconds, (float)ctx->textureTicks / SYSCLOCK_ARM11,
	       (float)ctx->shaderTicks / SYSCLOCK_ARM11, (float)ctx->primitiveTicks / SYSCLOCK_ARM11);
	printf("primitives: %d\n", ctx->primitiveCount);
	printf("textures: %d ok, %d failed, %d cached\n", ctx->texturesLoaded, ctx->texturesFailed, ctx->texturesCached);
	printf("effects: %d ok, %d fallback\n", ctx->effectsLoaded, ctx->effectsFallback);
	printf("texture memory: %u KB\n", (unsigned)(ctx->textureBytes / 1024));
	printf("linear used by level: %u KB\n", (unsigned)((linearBefore - freeLinear()) / 1024));
	printf("cameras: %d\n", (int)cameras.size());
	printf("water surfaces: %d, reflection %dx%d\n", (int)water.size(), reflectionTarget ? reflectionTarget->width() : 0,
	       reflectionTarget ? reflectionTarget->height() : 0);
	printf("report: %s\n", ctx->writeReport(userDataDir() + "/probe-log.txt") ? "written" : "FAILED");
	printf("\nPad move, C-Stick look, X/B updown\nL/R camera, Y free camera\nA cull, SELECT swap UV, dUP mode\ndDOWN lightmap, dLEFT/dRIGHT meshes\n(hold L for steps of 25), R camera\nZL reflection, ZR flip reflection\nSTART exits\n");

	int debugMode = 0;
	int meshLimit = 0;
	bool reflectionEnabled = true;
	const char* reflectionState = "off";
	u64 lastTick = svcGetSystemTick();
	float fpsAverage = 0.f;
	int frames = 0;

	while (aptMainLoop()) {
		hidScanInput();
		const u32 down = hidKeysDown();
		if (down & KEY_START) break;
		if (down & KEY_A) ctx->invertCulling = !ctx->invertCulling;
		if (down & KEY_DUP) debugMode = (debugMode + 1) % 4;
		if (down & KEY_DDOWN) ctx->lightmapMode = (ctx->lightmapMode + 1) % 3;
		const u32 held = hidKeysHeld();
		const int step = (held & KEY_L) ? 25 : 1;
		if ((down & KEY_DLEFT) || ((held & KEY_DLEFT) && (frames & 3) == 0)) meshLimit = std::max(0, meshLimit - step);
		if ((down & KEY_DRIGHT) || ((held & KEY_DRIGHT) && (frames & 3) == 0)) meshLimit += step;
		ctx->debugMode = debugMode;
		ctx->meshLimit = meshLimit;
		if (down & KEY_SELECT) ctx->swapUV = !ctx->swapUV;
		if (down & KEY_ZL) reflectionEnabled = !reflectionEnabled;
		if (down & KEY_ZR) ctx->reflectionFlip = (ctx->reflectionFlip + 1) % 4;
		if (down & KEY_Y) useFreeCamera = !useFreeCamera;
		if ((down & KEY_R) && !cameras.empty()) {
			cameraIndex = (cameraIndex + 1) % (int)cameras.size();
			camera = cameras[cameraIndex];
		}

		const u64 now = svcGetSystemTick();
		const float dt = std::min(0.1f, (float)(now - lastTick) / SYSCLOCK_ARM11);
		lastTick = now;
		ctx->time += dt;

		circlePosition move, look;
		hidCircleRead(&move);
		hidCstickRead(&look);
		if (useFreeCamera && camera) {
			freeCamera.update(dt, move, look);
			camera->setTransform(freeCamera.transform());
		}

		ctx->meshesDrawn = 0;
		for (int i = 0; i < 4; ++i) ctx->combinerUse[i] = 0;
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		ctx->setTarget(target, TOP_WIDTH, TOP_HEIGHT);
		ctx->reflectionTexture = 0;
		reflectionState = "off";
		if (reflectionEnabled && scene && camera && mirror)
			reflectionState = renderReflection(ctx, camera, mirror, water, reflectionTarget, (int)clearColor);
		ctx->clear((int)clearColor);
		if (scene && camera) camera->render(ctx);
		C3D_FrameEnd(0);

		const float gpuTime = C3D_GetDrawingTime();
		const float cpuTime = C3D_GetProcessingTime();
		fpsAverage += 1.f / std::max(0.0001f, dt);
		if (++frames >= 30) {
			static const char* const LIGHTMAP[] = { "mul2", "add ", "off " };
			static const char* const MODES[] = { "normal", "untextured", "no lighting", "lightmap only" };
			static const char* const FLIPS[] = { "none", "u", "v", "uv" };
			printf("\x1b[s\x1b[22;0Hfps %5.1f  cpu %4.1fms  gpu %4.1fms\nlinear %5u KB  vram %5u KB  cam %d\ncull %s  uv %s  lmap %s   \nmode %-14s meshes %d/%d    \nvtx %d tex %d lm %d water %d      \nrefl %-4s %-18.18s       \n%-38s\x1b[u",
			       fpsAverage / frames, cpuTime, gpuTime, (unsigned)(freeLinear() / 1024), (unsigned)(vramSpaceFree() / 1024),
			       cameraIndex, ctx->invertCulling ? "inv" : "nrm", ctx->swapUV ? "swap" : "nrm ",
			       LIGHTMAP[ctx->lightmapMode], MODES[debugMode], meshLimit, ctx->meshesDrawn,
			       ctx->combinerUse[0], ctx->combinerUse[1], ctx->combinerUse[2], ctx->combinerUse[3],
			       FLIPS[ctx->reflectionFlip], reflectionState, meshLimit > 0 ? ctx->isolatedMaterial : "");
			fpsAverage = 0.f;
			frames = 0;
		}
	}

	gr::citro3dShuttingDown = true;
	ctx->reflectionTexture = 0;
	reflectionTarget = 0;
	mirror = 0;
	scene = 0;
	resources = 0;
	camera = 0;
	cameras.clear();
	C3D_Fini();
	gfxExit();
	return 0;
}
