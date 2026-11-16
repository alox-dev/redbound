#pragma once
#include <hgr/Node.h>
#include <hgr/Scene.h>
#include <math/float3.h>
#include <functional>
#include <vector>

namespace gr { class Shader; }

namespace redbound {

struct WaterSurface {
	P(hgr::Node) node;
	float height;
	float bounds[4];
};

class WaterSurfaces {
public:
	WaterSurfaces(hgr::Scene* scene, const std::function<bool(gr::Shader*)>& isWater);

	const std::vector<WaterSurface>& all() const { return m_surfaces; }
	bool empty() const { return m_surfaces.empty(); }
	bool surfaceAt(float x, float z, float& height) const;
	const WaterSurface* nearest(const math::float3& from) const;

private:
	P(hgr::Scene) m_scene;
	std::vector<WaterSurface> m_surfaces;
};

}
