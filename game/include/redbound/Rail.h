#pragma once
#include "redbound/Physics.h"
#include <vector>

namespace redbound {

struct RailSample {
	Vec3 pos;
	Vec3 forward;
	Vec3 up;
	float s = 0.f;
	float distance = 0.f;
};

class RailTrack {
public:
	void build(const std::vector<Vec3>& track, const std::vector<Vec3>& normal);
	bool valid() const { return m_pts.size() >= 2; }
	float length() const { return m_cum.empty() ? 0.f : m_cum.back(); }
	RailSample closest(const Vec3& p) const;
	RailSample closestNear(const Vec3& p, float sHint, float back, float ahead) const;
	RailSample at(float s) const;

	static std::vector<Vec3> chain(const std::vector<std::pair<Vec3, Vec3>>& segments, std::vector<size_t>* breaks = 0);
	void setBreaks(const std::vector<size_t>& b);

private:
	std::vector<Vec3> m_pts, m_up;
	std::vector<float> m_cum;
	std::vector<bool> m_gap;
	RailSample sampleSegment(size_t i, float t) const;
};

}
