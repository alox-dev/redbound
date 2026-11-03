#include "redbound/Rail.h"
#include <cmath>

namespace redbound {

static Vec3 norm(const Vec3& v, const Vec3& fallback) { const float l = v.length(); return l > 1e-6f ? v * (1.f / l) : fallback; }

std::vector<Vec3> RailTrack::chain(const std::vector<std::pair<Vec3, Vec3>>& segments, std::vector<size_t>* breaks) {
	std::vector<Vec3> out;
	if (breaks) breaks->clear();
	const size_t n = segments.size();
	if (!n) return out;
	const float eps = 2.f;
	auto same = [&](const Vec3& a, const Vec3& b) { return (a - b).length() < eps; };
	auto dirOf = [](const Vec3& a, const Vec3& b) { const Vec3 d = b - a; const float l = d.length(); return l > 1e-6f ? d * (1.f / l) : Vec3(0, 0, 1); };
	std::vector<bool> used(n, false);
	auto pickStart = [&]() -> size_t {
		for (size_t i = 0; i < n; ++i) {
			if (used[i]) continue;
			bool continues = false;
			for (size_t k = 0; k < n && !continues; ++k)
				if (k != i && !used[k] && (same(segments[k].second, segments[i].first) || same(segments[k].first, segments[i].first))) continues = true;
			if (!continues) return i;
		}
		for (size_t i = 0; i < n; ++i) if (!used[i]) return i;
		return n;
	};
	size_t cur = pickStart();
	bool flip = false;
	while (cur < n) {
		used[cur] = true;
		const Vec3 a = flip ? segments[cur].second : segments[cur].first;
		const Vec3 b = flip ? segments[cur].first : segments[cur].second;
		if (out.empty()) out.push_back(a);
		else if (!same(out.back(), a)) { if (breaks) breaks->push_back(out.size()); out.push_back(a); }
		out.push_back(b);
		const Vec3 d = dirOf(a, b);
		size_t next = n;
		bool nextFlip = false;
		float bestDot = -2.f;
		for (size_t k = 0; k < n; ++k) {
			if (used[k]) continue;
			if (same(segments[k].first, b)) {
				const float dd = d.dot(dirOf(segments[k].first, segments[k].second));
				if (dd > bestDot) { bestDot = dd; next = k; nextFlip = false; }
			} else if (same(segments[k].second, b)) {
				const float dd = d.dot(dirOf(segments[k].second, segments[k].first));
				if (dd > bestDot) { bestDot = dd; next = k; nextFlip = true; }
			}
		}
		if (next == n) { next = pickStart(); nextFlip = false; }
		cur = next;
		flip = nextFlip;
	}
	return out;
}

void RailTrack::setBreaks(const std::vector<size_t>& b) {
	m_gap.assign(m_pts.size(), false);
	for (size_t k : b) if (k >= 1 && k - 1 < m_gap.size()) m_gap[k - 1] = true;
	m_cum.assign(m_pts.size(), 0.f);
	for (size_t i = 1; i < m_pts.size(); ++i) m_cum[i] = m_cum[i - 1] + (m_gap[i - 1] ? 0.f : (m_pts[i] - m_pts[i - 1]).length());
}

void RailTrack::build(const std::vector<Vec3>& track, const std::vector<Vec3>& normal) {
	m_pts = track;
	m_gap.assign(track.size(), false);
	m_up.assign(track.size(), Vec3(0, 1, 0));
	m_cum.assign(track.size(), 0.f);
	for (size_t i = 1; i < track.size(); ++i) m_cum[i] = m_cum[i - 1] + (track[i] - track[i - 1]).length();
	for (size_t i = 0; i < track.size(); ++i) {
		Vec3 np;
		if (normal.size() == track.size()) np = normal[i];
		else if (!normal.empty()) {
			float best = 1e30f;
			for (size_t k = 0; k < normal.size(); ++k) {
				const float d = (normal[k] - track[i]).length();
				if (d < best) { best = d; np = normal[k]; }
			}
		} else np = track[i] + Vec3(0, 1, 0);
		m_up[i] = norm(np - track[i], Vec3(0, 1, 0));
	}
}

RailSample RailTrack::sampleSegment(size_t i, float t) const {
	RailSample r;
	const Vec3 a = m_pts[i], b = m_pts[i + 1];
	r.pos = a + (b - a) * t;
	r.forward = norm(b - a, Vec3(0, 0, 1));
	r.up = norm(m_up[i] * (1.f - t) + m_up[i + 1] * t, Vec3(0, 1, 0));
	r.up = norm(r.up - r.forward * r.up.dot(r.forward), r.up);
	r.s = m_cum[i] + (m_cum[i + 1] - m_cum[i]) * t;
	return r;
}

RailSample RailTrack::closest(const Vec3& p) const {
	RailSample best;
	best.distance = 1e30f;
	for (size_t i = 0; i + 1 < m_pts.size(); ++i) {
		if (m_gap[i]) continue;
		const Vec3 a = m_pts[i], ab = m_pts[i + 1] - a;
		const float l2 = ab.dot(ab);
		float t = l2 > 1e-8f ? (p - a).dot(ab) / l2 : 0.f;
		t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
		const Vec3 q = a + ab * t;
		const float d = (p - q).length();
		if (d < best.distance) {
			best = sampleSegment(i, t);
			best.distance = d;
		}
	}
	return best;
}

RailSample RailTrack::closestNear(const Vec3& p, float sHint, float back, float ahead) const {
	const float lo = sHint - back, hi = sHint + ahead;
	RailSample best;
	best.distance = 1e30f;
	bool found = false;
	for (size_t i = 0; i + 1 < m_pts.size(); ++i) {
		if (m_gap[i] || m_cum[i + 1] < lo || m_cum[i] > hi) continue;
		const Vec3 a = m_pts[i], ab = m_pts[i + 1] - a;
		const float l2 = ab.dot(ab);
		float t = l2 > 1e-8f ? (p - a).dot(ab) / l2 : 0.f;
		t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
		const float d = (p - (a + ab * t)).length();
		if (d < best.distance) {
			best = sampleSegment(i, t);
			best.distance = d;
			found = true;
		}
	}
	return found ? best : closest(p);
}

RailSample RailTrack::at(float s) const {
	if (m_pts.size() < 2) return RailSample();
	if (s <= 0.f) return sampleSegment(0, 0.f);
	for (size_t i = 0; i + 1 < m_pts.size(); ++i) {
		if (m_gap[i]) continue;
		if (s <= m_cum[i + 1]) {
			const float seg = m_cum[i + 1] - m_cum[i];
			return sampleSegment(i, seg > 1e-6f ? (s - m_cum[i]) / seg : 0.f);
		}
	}
	return sampleSegment(m_pts.size() - 2, 1.f);
}

}
