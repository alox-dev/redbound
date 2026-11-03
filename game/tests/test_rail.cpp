#include "redbound/Rail.h"
#include <cmath>
#include <cstdio>
using namespace redbound;
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
	printf("segment chain and length\n");
	std::vector<std::pair<Vec3, Vec3>> segs = { { Vec3(0, 0, 0), Vec3(0, 0, 100) }, { Vec3(0, 0, 100), Vec3(100, 0, 100) } };
	std::vector<Vec3> pts = RailTrack::chain(segs);
	CHECK(pts.size() == 3);
	std::vector<Vec3> np;
	for (const Vec3& p : pts) np.push_back(p + Vec3(0, 10, 0));
	RailTrack r;
	r.build(pts, np);
	CHECK(std::fabs(r.length() - 200.f) < 1e-3f);

	printf("closest point, forward, up and travelled distance\n");
	RailSample s = r.closest(Vec3(5, 30, 50));
	CHECK(std::fabs(s.pos.z - 50.f) < 1e-3f && std::fabs(s.pos.x) < 1e-3f);
	CHECK(std::fabs(s.forward.z - 1.f) < 1e-3f);
	CHECK(std::fabs(s.up.y - 1.f) < 1e-3f);
	CHECK(std::fabs(s.s - 50.f) < 1e-3f);
	s = r.closest(Vec3(150, 0, 110));
	CHECK(std::fabs(s.s - 200.f) < 1e-3f);
	CHECK(std::fabs(r.at(150.f).pos.x - 50.f) < 1e-3f);

	printf("twisted track: up follows tracknormal\n");
	std::vector<Vec3> tube = { Vec3(0, 0, 0), Vec3(0, 0, 100) };
	std::vector<Vec3> side = { Vec3(10, 0, 0), Vec3(10, 0, 100) };
	RailTrack t;
	t.build(tube, side);
	CHECK(std::fabs(t.closest(Vec3(0, 0, 40)).up.x - 1.f) < 1e-3f);

	printf("loop: local search does not jump to the other side of the loop\n");
	{
		std::vector<Vec3> lp = { Vec3(0, 0, 0), Vec3(0, 0, 1000), Vec3(0, 60, 1000), Vec3(0, 60, 0) };
		std::vector<Vec3> ln;
		for (const Vec3& q : lp) ln.push_back(q + Vec3(0, 10, 0));
		RailTrack l;
		l.build(lp, ln);
		const Vec3 ball(0, 35, 500);
		const RailSample near = l.closestNear(ball, 500.f, 300.f, 300.f);
		CHECK(std::fabs(near.s - 500.f) < 1.f);
		CHECK(near.forward.z > 0.9f);
		const RailSample later = l.closestNear(ball, 1560.f, 300.f, 300.f);
		CHECK(later.forward.z < -0.9f);
	}

	printf("unordered and reversed segments are chained by connectivity\n");
	{
		std::vector<std::pair<Vec3, Vec3>> messy = {
			{ Vec3(0, 0, 200), Vec3(0, 0, 300) },
			{ Vec3(0, 0, 0), Vec3(0, 0, 100) },
			{ Vec3(0, 0, 200), Vec3(0, 0, 100) },
		};
		std::vector<Vec3> c = RailTrack::chain(messy);
		CHECK(c.size() == 4);
		bool increasing = true;
		for (size_t k = 1; k < c.size(); ++k) if (c[k].z <= c[k - 1].z) increasing = false;
		CHECK(increasing);
	}

	printf("track in two pieces: no imaginary segment between them\n");
	{
		std::vector<std::pair<Vec3, Vec3>> two = {
			{ Vec3(0, 0, 0), Vec3(0, 0, 1000) },
			{ Vec3(0, 0, 21000), Vec3(0, 0, 22000) },
		};
		std::vector<size_t> br;
		std::vector<Vec3> c = RailTrack::chain(two, &br);
		CHECK(br.size() == 1);
		std::vector<Vec3> nrm;
		for (const Vec3& q : c) nrm.push_back(q + Vec3(0, 10, 0));
		RailTrack r2;
		r2.build(c, nrm);
		r2.setBreaks(br);
		CHECK(std::fabs(r2.length() - 2000.f) < 1.f);
		const RailSample q = r2.closestNear(Vec3(0, 50, 21100), 990.f, 600.f, 3000.f);
		CHECK(q.distance < 60.f);
		CHECK(std::fabs(q.s - 1100.f) < 1.f);
	}

	printf("%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
