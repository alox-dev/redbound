#include <algorithm>
#include "WaterReflection.h"
#include <hgr/Mesh.h>
#include <gr/impl/DIPrimitive.h>
#include <math/float3x4.h>
#include <math/float4.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "stb_image_write.h"

namespace redbound {

WaterReflection::WaterReflection(gr::GLES2_Context* ctx, hgr::Scene* scene) : m_ctx(ctx), m_scene(scene) {
	m_surfaces.reset(new WaterSurfaces(scene, [](gr::Shader* shader) {
		gr::GLES2_Shader* fx = dynamic_cast<gr::GLES2_Shader*>(shader);
		return fx && fx->effect() && fx->effect()->isWater();
	}));
	for (int i = 1; i < 31; ++i) {
		char name[32];
		snprintf(name, sizeof(name), "water%02d.pvr", i);
		P(gr::Texture) t = ctx->createTexture(name);
		gr::GLES2_Texture* gt = dynamic_cast<gr::GLES2_Texture*>((gr::Texture*)t);
		if (!gt || !gt->loaded()) break;
		m_normals.push_back(t);
	}
	m_mirror = new hgr::Camera;
	m_mirror->setName("water_mirror");
	m_mirror->linkTo(scene);
}

void WaterReflection::update(hgr::Camera* camera, int width, int height, float time, int clearColor) {
	if (!m_normals.empty())
		m_ctx->waterNormal = m_normals[(size_t)(fmodf(time, 2.f) * 15.f) % m_normals.size()];
	if (!enabled || m_surfaces->empty()) { m_ctx->reflectionTexture = 0; return; }

	const math::float3x4 camTm = camera->worldTransform();
	const math::float3 camPos = camTm.translation();
	const WaterSurface* best = m_surfaces->nearest(camPos);
	static std::string lastState;
	std::string state;
	if (!best) state = "no reflection: no water linked to the scene";
	else if (camPos.y < best->height) state = "no reflection: camera below the water of " + std::string(best->node->name().c_str());
	else state = "reflecting " + std::string(best->node->name().c_str());
	if (getenv("REDBOUND_TRACE") && state != lastState) {
		printf("[trace] %s (t=%.1f s, camera y=%g, water y=%g)\n", state.c_str(), time, camPos.y, best ? best->height : 0.f);
		lastState = state;
	}
	if (!best || camPos.y < best->height) {
		m_ctx->reflectionTexture = 0;
		return;
	}
	const float h = best->height;

	const int tw = width / 2 > 1 ? width / 2 : 1, th = height / 2 > 1 ? height / 2 : 1;
	if (!m_target || tw != m_w || th != m_h) {
		m_target = m_ctx->createTexture(tw, th, gr::SurfaceFormat(gr::SurfaceFormat::SURFACE_A8R8G8B8), 0, 0);
		m_w = tw; m_h = th;
	}

	math::float3x4 refl(1.f);
	refl(1, 1) = -1.f;
	refl(1, 3) = 2.f * h;
	const math::float3x4 mirTm = refl * camTm;
	m_mirror->setHorizontalFov(camera->horizontalFov());
	m_mirror->setFront(camera->front());
	m_mirror->setBack(camera->back());
	m_mirror->setTransform(mirTm);

	const float cw[4] = { 0.f, 1.f, 0.f, -h };
	float cv[4];
	for (int j = 0; j < 4; ++j) {
		float acc = 0.f;
		for (int i = 0; i < 3; ++i) acc += cw[i] * mirTm(i, j);
		if (j == 3) acc += cw[3];
		cv[j] = acc;
	}

	m_ctx->setRenderTarget(m_target);
	m_ctx->setViewport(gr::Rect(0, 0, tw, th));
	m_ctx->clear(clearColor);
	m_ctx->forcedTechnique = "Reflected";
	m_ctx->mirrored = true;
	m_ctx->obliqueClip = true;
	m_ctx->clipPlane = math::float4(cv[0], cv[1], cv[2], cv[3]);
	m_ctx->reflectionTexture = 0;
	m_mirror->render(m_ctx);
	m_ctx->obliqueClip = false;
	m_ctx->mirrored = false;
	m_ctx->forcedTechnique.clear();
	m_ctx->setRenderTarget(0);
	m_ctx->setViewport(gr::Rect(0, 0, width, height));
	m_ctx->reflectionTexture = m_target;
}

}
