#pragma once
#include "SceneWorld.h"
#include "redbound/Physics.h"
#include <hgr/Scene.h>
#include <hgr/Camera.h>
#include <gr/Context.h>
#include <string>
#include <functional>
#include "redbound/Rail.h"
#include <vector>

namespace redbound {

class BallController {
public:
	BallController(hgr::Scene* scene, SceneWorld* world);

	void setFormInfo(const std::string& csv);
	void setBallModel(hgr::Node* model);
	void createShadow(gr::Context* ctx);
	void spawn();
	void addSphereTrigger(const std::string& trigger, hgr::Node* at, float radius);
	void addObjectShadow(hgr::Node* shadow, hgr::Node* owner, bool onlyNearGround = false);

	void update(float dt, float forward, float turn, bool jump);
	void setModernInput(bool modern, float mx, float my, float camYaw, float camPitch, bool followPath = false, bool autoFollow = false) {
		m_modernReq = modern; m_mx = mx; m_my = my; m_dCamYaw += camYaw; m_dCamPitch += camPitch; m_followPath = followPath; m_autoFollow = autoFollow;
	}
	bool modern() const { return m_modern; }
	bool controllable() const;

	void spawnAt(const std::string& nodeName);
	void updateChaseCamera(hgr::Camera* cam, float dt);

	const Ball& ball() const { return m_ball; }
	void loadRails();
	bool onRail() const { return m_rail >= 0; }
	bool hasRails() const { return !m_rails.empty(); }
	math::float3 modelCenter() const { return math::float3(m_modelOffset.x, m_modelOffset.y, m_modelOffset.z); }
	float railDistance() const { return m_railDistance; }
	void resetRailDistance() { m_railDistance = 0.f; }
	std::function<void(const std::string& file, float volume)> playSound;
	const CollisionWorld& collision() const { return m_world; }
	std::string spawnInfo;

private:
	struct Volume {
		std::string trigger;
		P(hgr::Node) node;
		bool sphere;
		float radius;
		math::float3 boxMin, boxMax;
		bool inside;
	};
	P(hgr::Scene) m_scene;
	SceneWorld* m_sw;
	CollisionWorld m_world;
	Ball m_ball;
	P(hgr::Node) m_model;
	std::vector<Volume> m_volumes;
	Vec3 m_camPos, m_camLook;
	bool m_camInit = false;
	Vec3 m_heading = Vec3(0, 0, 1);
	Vec3 m_modelOffset;
	std::string m_bodyName;
	float m_yaw = 0.f;
	float m_impactCooldown = 0.f;
	float m_roll = 0.f;
	P(hgr::Node) m_body;
	P(hgr::Node) m_shadow;
	bool m_ownShadow = false;
	math::float3x4 m_shadowRest = math::float3x4(1.f);
	P(hgr::Node) m_camPosNode, m_camLookNode;
	P(hgr::Node) m_eyes;
	math::float3x4 m_eyesRest = math::float3x4(1.f);
	math::float3 m_faceLocal = math::float3(0, 0, 1);
	math::float3 m_ballCenterLocal;
	Vec3 m_lastCam;
	bool m_haveCam = false;
	void updateEyes();
	struct Rail {
		std::string name, deathplane, next;
		RailTrack track;
		float force = 1000.f, forwardDir = 1.f;
	};
	std::vector<Rail> m_rails;
	int m_rail = -1;
	float m_railS = 0.f;
	float m_railDistance = 0.f;
	float m_railMaxSpeed = 3000.f;
	float m_railDt = 0.f;
	float m_railAirTime = 0.f;
	std::vector<Vec3> m_trail;
	std::vector<float> m_trailS;
	int m_trailTick = 0;
	bool m_railDead = false;
	RailSample m_railSample;
	int findRail(const std::string& name) const;
	void processRequests();
	void railInput(float turn, BallInput& in);
	bool m_cinematic = false;
	Vec3 m_cinematicCam;
	Vec3 m_eyeDir;
	float m_eyeDt = 0.f;
	Vec3 m_camUp = Vec3(0, 1, 0);
	bool m_modernReq = false, m_modern = false;
	bool m_followPath = false;
	bool m_autoFollow = false;
	struct Guide {
		std::string name;
		RailTrack track, position, lookAt;
	};
	std::vector<Guide> m_guides;
	int m_guide = -1;
	float m_guideS = 0.f;
	bool m_useGuide = false;
public:
	void setGuidedCamera(bool on) { m_useGuide = on; }
	bool hasGuide() const { return m_guide >= 0; }
private:
	float m_autoFollowBlend = 0.f;
	float m_eyeDistLocal = 0.f;
	float m_mx = 0.f, m_my = 0.f, m_dCamYaw = 0.f, m_dCamPitch = 0.f;
	float m_camYaw = 0.f, m_camPitch = 0.59f;
	struct ObjShadow { P(hgr::Node) shadow; P(hgr::Node) owner; math::float3x4 rest; bool traced = false; bool onlyNearGround = false; };
	std::vector<ObjShadow> m_objShadows;
	void updateObjectShadows();
	std::string m_shadowName = "shadow_bounce";
	BallParams m_formDefaults;
	bool m_hasForm = false;
	void publishBallValues(const BallParams& p);
	void readBallValues();
	void updateShadow();
	math::float3x4 m_bodyRest;

	void buildCollision();
	void checkVolumes();
	bool insideVolume(const Volume& v, const Vec3& p) const;
};

}
