#pragma once
#include "GLES2.h"
#include "WaterSurfaces.h"
#include <hgr/Camera.h>
#include <hgr/Scene.h>
#include <memory>
#include <vector>

namespace redbound {

class WaterReflection {
public:
	WaterReflection(gr::GLES2_Context* ctx, hgr::Scene* scene);

	void update(hgr::Camera* camera, int width, int height, float time, int clearColor);

	size_t waterCount() const { return m_surfaces->all().size(); }
	bool surfaceAt(float x, float z, float& height) const { return m_surfaces->surfaceAt(x, z, height); }
	int normalFrames() const { return (int)m_normals.size(); }
	bool enabled = true;

private:
	P(gr::GLES2_Context) m_ctx;
	P(hgr::Scene) m_scene;
	std::unique_ptr<WaterSurfaces> m_surfaces;
	std::vector<P(gr::Texture)> m_normals;
	P(gr::Texture) m_target;
	P(hgr::Camera) m_mirror;
	int m_w = 0, m_h = 0;
};

}
