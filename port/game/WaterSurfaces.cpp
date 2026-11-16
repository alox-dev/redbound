#include "WaterSurfaces.h"
#include <gr/impl/DIPrimitive.h>
#include <hgr/Mesh.h>
#include <math/float3x4.h>
#include <math/float4.h>
#include <algorithm>

namespace redbound {

static bool measure(hgr::Node* n, float& height, float* bounds) {
	hgr::Mesh* m = static_cast<hgr::Mesh*>(n);
	const math::float3x4 w = n->worldTransform();
	double sum = 0;
	long count = 0;
	for (int i = 0; i < m->primitives(); ++i) {
		gr::DIPrimitive* p = dynamic_cast<gr::DIPrimitive*>(m->getPrimitive(i));
		if (!p || p->vertices() <= 0) continue;
		std::vector<math::float4> pos(p->vertices());
		p->lock(gr::ContextObject::LOCK_READ);
		p->getVertexPositions(0, &pos[0], (int)pos.size());
		p->unlock();
		for (size_t k = 0; k < pos.size(); ++k) {
			const math::float3 v = w.transform(math::float3(pos[k].x, pos[k].y, pos[k].z));
			sum += v.y;
			if (!count) {
				bounds[0] = bounds[1] = v.x;
				bounds[2] = bounds[3] = v.z;
			}
			bounds[0] = std::min(bounds[0], v.x);
			bounds[1] = std::max(bounds[1], v.x);
			bounds[2] = std::min(bounds[2], v.z);
			bounds[3] = std::max(bounds[3], v.z);
			++count;
		}
	}
	if (!count) return false;
	height = (float)(sum / count);
	return true;
}

WaterSurfaces::WaterSurfaces(hgr::Scene* scene, const std::function<bool(gr::Shader*)>& isWater) : m_scene(scene) {
	for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
		if (n->classId() != hgr::Node::NODE_MESH) continue;
		hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
		bool water = false;
		for (int i = 0; i < mesh->primitives() && !water; ++i) water = isWater(mesh->getPrimitive(i)->shader());
		if (!water) continue;
		WaterSurface s;
		s.node = n;
		if (measure(n, s.height, s.bounds)) m_surfaces.push_back(s);
	}
}

bool WaterSurfaces::surfaceAt(float x, float z, float& height) const {
	bool found = false;
	for (const WaterSurface& w : m_surfaces)
		if (x >= w.bounds[0] && x <= w.bounds[1] && z >= w.bounds[2] && z <= w.bounds[3] && (!found || w.height > height)) {
			height = w.height;
			found = true;
		}
	return found;
}

const WaterSurface* WaterSurfaces::nearest(const math::float3& from) const {
	const WaterSurface* best = 0;
	float bestDistance = 1e30f;
	for (const WaterSurface& w : m_surfaces) {
		bool linked = false;
		for (hgr::Node* p = w.node->parent(); p; p = p->parent())
			if (p == (hgr::Node*)m_scene) {
				linked = true;
				break;
			}
		if (!linked || !w.node->enabled()) continue;
		const math::float3 d = w.node->worldTransform().translation() - from;
		const float dd = d.x * d.x + d.y * d.y + d.z * d.z;
		if (dd < bestDistance) {
			bestDistance = dd;
			best = &w;
		}
	}
	return best;
}

}
