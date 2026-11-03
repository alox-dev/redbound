#include "redbound/Physics.h"
#include <cstdio>
#include <cmath>

using namespace redbound;
static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void addFloor(CollisionWorld& w, float y = 0, float half = 20000) {
	CollisionMesh m; m.name = "piso";
	const int id = w.addMesh(m);
	w.addTriangle(id, Vec3(-half, y, -half), Vec3(-half, y, half), Vec3(half, y, half));
	w.addTriangle(id, Vec3(-half, y, -half), Vec3(half, y, half), Vec3(half, y, -half));
}

int main() {
	printf("falls and rests on the floor\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.params.radius = 30; b.reset(Vec3(0, 200, 0));
		for (int i = 0; i < 240; ++i) b.update(1.f / 60, BallInput(), w);
		CHECK(b.onGround);
		CHECK(std::fabs(b.position.y - b.params.radius) < 2.f);
		CHECK(b.velocity.length() < 5.f);
	}
	printf("rolls with input and slows down without it\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.params.radius = 30; b.reset(Vec3(0, 30, 0));
		BallInput in; in.move = Vec3(1, 0, 0);
		for (int i = 0; i < 60; ++i) b.update(1.f / 60, in, w);
		CHECK(b.position.x > 100.f);
		CHECK(b.rollAngle > 1.f);
		for (int i = 0; i < 300; ++i) b.update(1.f / 60, BallInput(), w);
		CHECK(Vec3(b.velocity.x, 0, b.velocity.z).length() < 1.f);
	}
	printf("jumps only from the ground\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.params.radius = 30; b.reset(Vec3(0, 30, 0));
		for (int i = 0; i < 10; ++i) b.update(1.f / 60, BallInput(), w);
		BallInput j; j.jump = true;
		b.update(1.f / 60, j, w);
		float maxY = b.position.y;
		for (int i = 0; i < 30; ++i) { b.update(1.f / 60, j, w); maxY = std::max(maxY, b.position.y); }
		CHECK(maxY > 100.f);
	}
	printf("does not pass through a wall at high speed\n");
	{
		CollisionWorld w; addFloor(w);
		CollisionMesh m; m.name = "pared"; const int id = w.addMesh(m);
		w.addTriangle(id, Vec3(300, 0, -500), Vec3(300, 500, 500), Vec3(300, 0, 500));
		w.addTriangle(id, Vec3(300, 0, -500), Vec3(300, 500, -500), Vec3(300, 500, 500));
		Ball b; b.reset(Vec3(0, 30, 0)); b.velocity = Vec3(2000, 0, 0); b.params.maxSpeed = 5000; b.params.radius = 30;
		for (int i = 0; i < 60; ++i) b.update(1.f / 60, BallInput(), w);
		CHECK(b.position.x < 300.f);
	}
	printf("records the touched mesh (for executetrigger)\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.params.radius = 30; b.reset(Vec3(0, 29, 0));
		b.update(1.f / 60, BallInput(), w);
		CHECK(!b.touched.empty() && w.mesh(b.touched[0]).name == "piso");
	}
	printf("downward ray finds the floor\n");
	{
		CollisionWorld w; addFloor(w, 50);
		float y = 0;
		CHECK(w.rayDown(Vec3(10, 500, 10), 2000, y) && std::fabs(y - 50) < 0.01f);
	}
	printf("original values: max speed and jump height\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.params.maxSpeed = 600; b.reset(Vec3(0, 50, 0));
		BallInput in; in.move = Vec3(1, 0, 0);
		for (int i = 0; i < 300; ++i) b.update(1.f / 60, in, w);
		CHECK(b.velocity.length() <= 600.5f && b.velocity.length() > 550.f);
		Ball c; c.params.jumpMultiplier = 1.f; c.reset(Vec3(0, 50, 0));
		for (int i = 0; i < 10; ++i) c.update(1.f / 60, BallInput(), w);
		BallInput j; j.jump = true;
		float maxY = 0;
		for (int i = 0; i < 60; ++i) { c.update(1.f / 60, j, w); j.jump = false; maxY = std::max(maxY, c.position.y); }
		CHECK(maxY - 50.f > 140.f && maxY - 50.f < 175.f);
		Ball d; d.params.maxSpeed = 600; d.params.jumpMultiplier = 1.f; d.reset(Vec3(0, 50, 0));
		for (int i = 0; i < 10; ++i) d.update(1.f / 60, BallInput(), w);
		BallInput j2; j2.jump = true;
		maxY = 0;
		for (int i = 0; i < 60; ++i) { d.update(1.f / 60, j2, w); j2.jump = false; maxY = std::max(maxY, d.position.y); }
		CHECK(maxY - 50.f > 140.f);
	}
	printf("falling accelerates beyond ballmaxspeed\n");
	{
		CollisionWorld w; addFloor(w, -100000);
		Ball b; b.params.maxSpeed = 600; b.reset(Vec3(0, 5000, 0));
		for (int i = 0; i < 60; ++i) b.update(1.f / 60, BallInput(), w);
		CHECK(-b.velocity.y > 1500.f);
	}
	printf("jumps even if contact was lost for a moment (original 0.5 s grace)\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.params.jumpMultiplier = 1.f; b.reset(Vec3(0, 50, 0));
		for (int i = 0; i < 10; ++i) b.update(1.f / 60, BallInput(), w);
		b.position.y += 3.f; b.onGround = false;
		BallInput j; j.jump = true;
		b.update(1.f / 60, j, w);
		CHECK(b.velocity.y > 700.f);
		const float vy = b.velocity.y;
		b.update(1.f / 60, j, w);
		CHECK(b.velocity.y < vy);
	}
	printf("records the jump and the impact strength (for sounds)\n");
	{
		CollisionWorld w; addFloor(w);
		Ball b; b.reset(Vec3(0, 600, 0));
		float maxImpact = 0;
		for (int i = 0; i < 60; ++i) { b.update(1.f / 60, BallInput(), w); maxImpact = std::max(maxImpact, b.impactSpeed); }
		CHECK(maxImpact > 800.f);
		BallInput j; j.jump = true;
		b.update(1.f / 60, j, w);
		CHECK(b.jumped);
		b.update(1.f / 60, BallInput(), w);
		CHECK(!b.jumped);
	}
	printf("gravity in another direction (rail): falls and rests on a wall\n");
	{
		CollisionWorld w;
		CollisionMesh m; m.name = "pared"; const int id = w.addMesh(m);
		w.addTriangle(id, Vec3(0, -2000, -2000), Vec3(0, 2000, -2000), Vec3(0, 2000, 2000));
		w.addTriangle(id, Vec3(0, -2000, -2000), Vec3(0, 2000, 2000), Vec3(0, -2000, 2000));
		Ball b; b.up = Vec3(1, 0, 0); b.reset(Vec3(300, 0, 0));
		for (int i = 0; i < 120; ++i) b.update(1.f / 60, BallInput(), w);
		CHECK(std::fabs(b.position.x - b.params.radius) < 2.f);
		CHECK(b.onGround);
		BallInput j; j.jump = true;
		b.update(1.f / 60, j, w);
		CHECK(b.velocity.x > 500.f);
	}
	printf("ray in any direction: ceiling upwards and wall sideways\n");
	{
		CollisionWorld w; addFloor(w, 500.f);
		Vec3 hit, n;
		CHECK(w.ray(Vec3(10, 0, 10), Vec3(0, 1, 0), 2000.f, hit, n));
		CHECK(std::fabs(hit.y - 500.f) < 0.5f && n.y < -0.9f);
		CHECK(!w.ray(Vec3(10, 0, 10), Vec3(0, -1, 0), 2000.f, hit, n));
		CHECK(!w.ray(Vec3(10, 0, 10), Vec3(0, 1, 0), 300.f, hit, n));
	}
	printf("sensors are passable but record contact\n");
	{
		CollisionWorld w; addFloor(w);
		CollisionMesh m; m.name = "sensor"; m.solid = false; const int id = w.addMesh(m);
		w.addTriangle(id, Vec3(300, 0, -500), Vec3(300, 500, 500), Vec3(300, 0, 500));
		w.addTriangle(id, Vec3(300, 0, -500), Vec3(300, 500, -500), Vec3(300, 500, 500));
		Ball b; b.params.radius = 30; b.reset(Vec3(0, 30, 0)); b.velocity = Vec3(400, 0, 0);
		bool sensed = false;
		BallInput go; go.move = Vec3(1, 0, 0);
		for (int i = 0; i < 90; ++i) {
			b.update(1.f / 60, go, w);
			for (int o : b.touched) if (w.mesh(o).name == "sensor") sensed = true;
		}
		CHECK(b.position.x > 330.f);
		CHECK(sensed);
	}
	printf("does not hang with absurd values (huge radius, non-finite position)\n");
	{
		CollisionWorld w; addFloor(w);
		std::vector<Contact> c;
		w.sphereContacts(Vec3(0, 0, 0), 1e10f, c);
		CHECK(c.empty());
		w.sphereContacts(Vec3(NAN, 0, 0), 30.f, c);
		CHECK(c.empty());
		Ball b; b.params.radius = 1e10f; b.reset(Vec3(0, 100, 0));
		b.update(1.f / 60, BallInput(), w);
		CHECK(true);
	}
	printf("%d checks, %d failures\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
