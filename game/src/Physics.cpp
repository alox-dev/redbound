#include "redbound/Physics.h"
#include <algorithm>

namespace redbound {

void CollisionWorld::addTriangle(int owner, const Vec3& a, const Vec3& b, const Vec3& c) {
	CollisionTriangle t;
	t.a = a; t.b = b; t.c = c; t.owner = owner;
	t.n = (b - a).cross(c - a).normalized();
	if (t.n.length() < 0.5f) return;
	const int idx = (int)m_tris.size();
	m_tris.push_back(t);
	const int x0 = cellOf(std::min({ a.x, b.x, c.x })), x1 = cellOf(std::max({ a.x, b.x, c.x }));
	const int y0 = cellOf(std::min({ a.y, b.y, c.y })), y1 = cellOf(std::max({ a.y, b.y, c.y }));
	const int z0 = cellOf(std::min({ a.z, b.z, c.z })), z1 = cellOf(std::max({ a.z, b.z, c.z }));
	for (int x = x0; x <= x1; ++x)
		for (int y = y0; y <= y1; ++y)
			for (int z = z0; z <= z1; ++z)
				m_grid[key(x, y, z)].push_back(idx);
}

static Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
	const Vec3 ab = b - a, ac = c - a, ap = p - a;
	const float d1 = ab.dot(ap), d2 = ac.dot(ap);
	if (d1 <= 0 && d2 <= 0) return a;
	const Vec3 bp = p - b;
	const float d3 = ab.dot(bp), d4 = ac.dot(bp);
	if (d3 >= 0 && d4 <= d3) return b;
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
	const Vec3 cp = p - c;
	const float d5 = ab.dot(cp), d6 = ac.dot(cp);
	if (d6 >= 0 && d5 <= d6) return c;
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
	const float va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	const float denom = 1.f / (va + vb + vc);
	return a + ab * (vb * denom) + ac * (vc * denom);
}

void CollisionWorld::sphereContacts(const Vec3& center, float radius, std::vector<Contact>& out) const {
	out.clear();
	if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !(radius > 0.f) || radius > m_cell * 64.f)
		return;
	const int x0 = cellOf(center.x - radius), x1 = cellOf(center.x + radius);
	const int y0 = cellOf(center.y - radius), y1 = cellOf(center.y + radius);
	const int z0 = cellOf(center.z - radius), z1 = cellOf(center.z + radius);
	std::vector<int> seen;
	for (int x = x0; x <= x1; ++x)
		for (int y = y0; y <= y1; ++y)
			for (int z = z0; z <= z1; ++z) {
				std::unordered_map<long long, std::vector<int> >::const_iterator it = m_grid.find(key(x, y, z));
				if (it == m_grid.end()) continue;
				for (int ti : it->second) {
					if (std::find(seen.begin(), seen.end(), ti) != seen.end()) continue;
					seen.push_back(ti);
					const CollisionTriangle& t = m_tris[ti];
					const Vec3 q = closestOnTriangle(center, t.a, t.b, t.c);
					const Vec3 d = center - q;
					const float dist = d.length();
					if (dist >= radius) continue;
					Contact c;
					c.normal = dist > 1e-4f ? d * (1.f / dist) : t.n;
					c.depth = radius - dist;
					c.owner = t.owner;
					out.push_back(c);
				}
			}
}

bool CollisionWorld::rayDown(const Vec3& from, float maxDist, float& hitY) const {
	Vec3 n;
	return rayDown(from, maxDist, hitY, n);
}

bool CollisionWorld::rayDown(const Vec3& from, float maxDist, float& hitY, Vec3& normal) const {
	if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(from.z) || maxDist > m_cell * 10000.f) return false;
	bool hit = false;
	float best = from.y - maxDist;
	const int cx = cellOf(from.x), cz = cellOf(from.z);
	const int y1 = cellOf(from.y), y0 = cellOf(from.y - maxDist);
	for (int y = y1; y >= y0; --y) {
		std::unordered_map<long long, std::vector<int> >::const_iterator it = m_grid.find(key(cx, y, cz));
		if (it == m_grid.end()) continue;
		for (int ti : it->second) {
			const CollisionTriangle& t = m_tris[ti];
			if (std::fabs(t.n.y) < 1e-4f) continue;
			const float py = t.a.y - (t.n.x * (from.x - t.a.x) + t.n.z * (from.z - t.a.z)) / t.n.y;
			if (py > from.y || py < best) continue;
			const Vec3 p(from.x, py, from.z);
			const Vec3 c0 = (t.b - t.a).cross(p - t.a), c1 = (t.c - t.b).cross(p - t.b), c2 = (t.a - t.c).cross(p - t.c);
			const bool inside = (c0.y >= 0 && c1.y >= 0 && c2.y >= 0) || (c0.y <= 0 && c1.y <= 0 && c2.y <= 0);
			if (!inside) continue;
			best = py;
			hit = true;
			normal = t.n.y < 0 ? t.n * -1.f : t.n;
		}
	}
	if (hit) hitY = best;
	return hit;
}

bool CollisionWorld::ray(const Vec3& from, const Vec3& dir, float maxDist, Vec3& hit, Vec3& normal) const {
	if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(from.z) || !(maxDist > 0.f) || maxDist > m_cell * 10000.f) return false;
	float best = maxDist;
	bool found = false;
	std::vector<int> seen;
	const int steps = (int)std::ceil(maxDist / (m_cell * 0.5f)) + 1;
	for (int i = 0; i <= steps; ++i) {
		const Vec3 p = from + dir * std::min(maxDist, i * m_cell * 0.5f);
		const int cx = cellOf(p.x), cy = cellOf(p.y), cz = cellOf(p.z);
		for (int x = cx - 1; x <= cx + 1; ++x)
			for (int y = cy - 1; y <= cy + 1; ++y)
				for (int z = cz - 1; z <= cz + 1; ++z) {
					std::unordered_map<long long, std::vector<int> >::const_iterator it = m_grid.find(key(x, y, z));
					if (it == m_grid.end()) continue;
					for (int ti : it->second) {
						if (std::find(seen.begin(), seen.end(), ti) != seen.end()) continue;
						seen.push_back(ti);
						const CollisionTriangle& t = m_tris[ti];
						if (t.owner >= 0 && t.owner < (int)m_meshes.size() && !m_meshes[t.owner].solid) continue;
						const Vec3 e1 = t.b - t.a, e2 = t.c - t.a, pv = dir.cross(e2);
						const float det = e1.dot(pv);
						if (std::fabs(det) < 1e-8f) continue;
						const float inv = 1.f / det;
						const Vec3 tv = from - t.a;
						const float u = tv.dot(pv) * inv;
						if (u < 0.f || u > 1.f) continue;
						const Vec3 qv = tv.cross(e1);
						const float v = dir.dot(qv) * inv;
						if (v < 0.f || u + v > 1.f) continue;
						const float d = e2.dot(qv) * inv;
						if (d < 0.f || d >= best) continue;
						best = d;
						found = true;
						normal = t.n.dot(dir) > 0.f ? t.n * -1.f : t.n;
					}
				}
		if (found && i * m_cell * 0.5f > best + m_cell) break;
	}
	if (found) hit = from + dir * best;
	return found;
}

void Ball::update(float dt, const BallInput& in, const CollisionWorld& world) {
	if (dt > 0.1f) dt = 0.1f;
	if (!(dt > 0.f)) return;
	touched.clear();
	jumped = false;
	impactSpeed = 0.f;
	const float speed = velocity.length();
	int steps = (int)std::ceil(speed * dt / (params.radius * 0.4f));
	steps = std::max(1, std::min(steps, 16));
	for (int i = 0; i < steps; ++i) step(dt / steps, in, world);
}

void Ball::step(float dt, const BallInput& in, const CollisionWorld& world) {
	const float ul = up.length();
	const Vec3 up = ul > 1e-6f ? this->up * (1.f / ul) : Vec3(0, 1, 0);

	Vec3 move = in.move;
	if (onGround) move = (move - groundNormal * move.dot(groundNormal));
	const float invMass = params.mass > 1e-6f ? 1.f / params.mass : 1.f;
	const float control = onGround ? 1.f : params.airControl;
	velocity += move * (params.movementForce * control * invMass * dt);
	velocity -= up * (params.gravity * dt);

	sinceContact += dt;
	if (in.jump && sinceContact < 0.5f) {
		velocity -= up * velocity.dot(up);
		velocity += up * (params.jumpForce * params.jumpMultiplier * invMass);
		onGround = false;
		jumped = true;
		sinceContact = 1e9f;
	}

	if (onGround && in.move.length() < 0.01f) {
		Vec3 tangential = velocity - groundNormal * velocity.dot(groundNormal);
		const float ts = tangential.length();
		if (ts > 1e-4f) velocity -= tangential * (std::min(ts, params.friction * dt) / ts);
	}

	const Vec3 vert = up * velocity.dot(up);
	const Vec3 horiz = velocity - vert;
	const float hs = horiz.length();
	if (hs > params.maxSpeed) velocity = vert + horiz * (params.maxSpeed / hs);
	const float sp = velocity.length();
	if (sp > params.terminalSpeed) velocity = velocity * (params.terminalSpeed / sp);

	position += velocity * dt;

	std::vector<Contact> contacts;
	bool ground = false;
	Vec3 gn(0, 0, 0);
	for (int iter = 0; iter < 4; ++iter) {
		world.sphereContacts(position, params.radius, contacts);
		if (contacts.empty()) break;
		const Contact* deepest = 0;
		for (const Contact& c : contacts) {
			if (std::find(touched.begin(), touched.end(), c.owner) == touched.end()) touched.push_back(c.owner);
			if (!world.mesh(c.owner).solid) continue;
			if (c.normal.dot(up) > 0.55f) { ground = true; gn += c.normal; }
			if (!deepest || c.depth > deepest->depth) deepest = &c;
		}
		if (!deepest) break;
		const Contact& c = *deepest;
		position += c.normal * (c.depth + 0.01f);
		const float vn = velocity.dot(c.normal);
		if (-vn > impactSpeed) { impactSpeed = -vn; impactNormal = c.normal; }
		if (vn < 0) {
			const float bounce = (-vn > params.gravity * 0.05f) ? params.rebound : 0.f;
			velocity -= c.normal * (vn * (1.f + bounce));
		}
	}
	onGround = ground;
	if (ground) { groundNormal = gn.normalized(); if (sinceContact < 1e8f || velocity.dot(up) <= 0.f) sinceContact = 0.f; }

	const Vec3 h = velocity - up * velocity.dot(up);
	const float hl = h.length();
	if (hl > 1e-3f) {
		rollAxis = up.cross(h * (1.f / hl));
		rollAngle += hl * dt / params.radius;
	}
}

}
