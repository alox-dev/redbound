#pragma once
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace redbound {

struct Vec3 {
	float x = 0, y = 0, z = 0;
	Vec3() {}
	Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
	Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
	Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
	Vec3 operator*(float k) const { return Vec3(x * k, y * k, z * k); }
	Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
	Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
	Vec3 cross(const Vec3& o) const { return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }
	float length() const { return std::sqrt(dot(*this)); }
	Vec3 normalized() const { const float l = length(); return l > 1e-9f ? *this * (1.f / l) : Vec3(0, 0, 0); }
};

struct CollisionTriangle {
	Vec3 a, b, c, n;
	int owner;
};

struct CollisionMesh {
	std::string name;
	int group = 0;
	std::string executeTrigger;
	bool solid = true;
};

struct Contact {
	Vec3 normal;
	float depth = 0;
	int owner = -1;
};

class CollisionWorld {
public:
	explicit CollisionWorld(float cellSize = 200.f) : m_cell(cellSize) {}

	int addMesh(const CollisionMesh& m) { m_meshes.push_back(m); return (int)m_meshes.size() - 1; }
	void addTriangle(int owner, const Vec3& a, const Vec3& b, const Vec3& c);
	const CollisionMesh& mesh(int i) const { return m_meshes[i]; }
	size_t meshCount() const { return m_meshes.size(); }
	size_t triangleCount() const { return m_tris.size(); }

	void sphereContacts(const Vec3& center, float radius, std::vector<Contact>& out) const;

	bool rayDown(const Vec3& from, float maxDist, float& hitY) const;
	bool rayDown(const Vec3& from, float maxDist, float& hitY, Vec3& normal) const;
	bool ray(const Vec3& from, const Vec3& dir, float maxDist, Vec3& hit, Vec3& normal) const;

private:
	float m_cell;
	std::vector<CollisionMesh> m_meshes;
	std::vector<CollisionTriangle> m_tris;
	std::unordered_map<long long, std::vector<int> > m_grid;
	long long key(int x, int y, int z) const { return ((long long)(x & 0x1FFFFF) << 42) | ((long long)(y & 0x1FFFFF) << 21) | (long long)(z & 0x1FFFFF); }
	int cellOf(float v) const { return (int)std::floor(v / m_cell); }
};

struct BallInput {
	Vec3 move;
	bool jump = false;
};

struct BallParams {
	float radius = 50.f;
	float mass = 1.f;
	float gravity = 2000.f;
	float movementForce = 1750.f;
	float jumpForce = 800.f;
	float friction = 350.f;
	float frictionSlide = 0.4f;
	float maxSpeed = 800.f;
	float terminalSpeed = 10000.f;
	float jumpMultiplier = 0.85f;
	float rebound = 0.1f;
	float airControl = 0.f;
	float killY = -2000.f;
};

class Ball {
public:
	BallParams params;
	Vec3 position, velocity;
	bool onGround = false;
	Vec3 groundNormal = Vec3(0, 1, 0);
	float sinceContact = 1e9f;
	float rollAngle = 0.f;
	Vec3 rollAxis = Vec3(1, 0, 0);
	std::vector<int> touched;
	bool jumped = false;
	Vec3 up = Vec3(0, 1, 0);
	float impactSpeed = 0.f;
	Vec3 impactNormal;

	void reset(const Vec3& p) { position = p; velocity = Vec3(); onGround = false; touched.clear(); }
	void update(float dt, const BallInput& in, const CollisionWorld& world);

private:
	void step(float dt, const BallInput& in, const CollisionWorld& world);
};

}
