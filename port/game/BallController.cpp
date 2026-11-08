#include "BallController.h"
#include <hgr/Mesh.h>
#include <hgr/Dummy.h>
#include <gr/Shader.h>
#include <gr/Texture.h>
#include <gr/VertexFormat.h>
#include <math/float4.h>
#include <lang/Throwable.h>
#include <hgr/Lines.h>
#include <hgr/UserPropertySet.h>
#include <gr/impl/DIPrimitive.h>
#include <lang/Hashtable.h>
#include <math/float3x3.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace redbound {

static std::string prop(const std::string& props, const std::string& key) {
	size_t at = props.find(key + "=");
	while (at != std::string::npos && at > 0 && props[at - 1] != '\n') at = props.find(key + "=", at + 1);
	if (at == std::string::npos) return std::string();
	size_t e = props.find('\n', at);
	std::string v = props.substr(at + key.size() + 1, e == std::string::npos ? std::string::npos : e - at - key.size() - 1);
	while (!v.empty() && (v[v.size() - 1] == '\r' || v[v.size() - 1] == ' ')) v.erase(v.size() - 1);
	return v;
}

static Vec3 toVec(const math::float3& v) { return Vec3(v.x, v.y, v.z); }

BallController::BallController(hgr::Scene* scene, SceneWorld* world) : m_scene(scene), m_sw(world) {
	buildCollision();
}

void BallController::buildCollision() {
	hgr::UserPropertySet* ups = m_scene->userProperties();
	if (!ups) return;
	for (lang::HashtableIterator<lang::String, lang::String> it = ups->begin(); it != ups->end(); ++it) {
		const std::string name = it.key().c_str(), props = it.value().c_str();
		const std::string cm = prop(props, "collisionmesh");
		const std::string ctype = prop(props, "collisiontype");
		if (name.compare(0, 2, "T_") == 0 && !ctype.empty()) {
			hgr::Node* n = m_scene->findNodeByName(name.c_str());
			if (!n) continue;
			Volume v;
			v.trigger = name;
			v.node = n;
			v.sphere = ctype == "SPHERE" || ctype == "sphere";
			v.radius = (float)atof(prop(props, "radius").c_str());
			v.inside = false;
			if (n->classId() == hgr::Node::NODE_DUMMY) {
				hgr::Dummy* d = static_cast<hgr::Dummy*>(n);
				v.boxMin = d->boxMin();
				v.boxMax = d->boxMax();
			}
			m_volumes.push_back(v);
			continue;
		}
		if (cm.empty()) continue;
		hgr::Node* n = m_scene->findNodeByName((cm == "this" ? name : cm).c_str());
		if (!n || n->classId() != hgr::Node::NODE_MESH) continue;
		CollisionMesh meta;
		meta.name = name;
		meta.group = atoi(prop(props, "group").c_str());
		meta.executeTrigger = prop(props, "executetrigger");
		meta.solid = (meta.group & 0x400) == 0;
		const int id = m_world.addMesh(meta);
		hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
		const math::float3x4 w = n->worldTransform();
		for (int pi = 0; pi < mesh->primitives(); ++pi) {
			gr::DIPrimitive* prim = dynamic_cast<gr::DIPrimitive*>(mesh->getPrimitive(pi));
			if (!prim || prim->vertices() < 3 || prim->type() != gr::Primitive::PRIM_TRI) continue;
			std::vector<math::float4> pos(prim->vertices());
			prim->lock(gr::ContextObject::LOCK_READ);
			prim->getVertexPositions(0, &pos[0], (int)pos.size());
			std::vector<int> idx;
			if (prim->indices() > 0) {
				uint16_t* ip = 0;
				int isz = 0;
				prim->getIndexDataPtr(&ip, &isz);
				idx.assign(ip, ip + prim->indices());
			} else {
				for (int k = 0; k < prim->vertices(); ++k) idx.push_back(k);
			}
			prim->unlock();
			for (size_t k = 0; k + 2 < idx.size(); k += 3) {
				const math::float4& A = pos[idx[k]], &B = pos[idx[k + 1]], &C = pos[idx[k + 2]];
				m_world.addTriangle(id, toVec(w.transform(math::float3(A.x, A.y, A.z))),
				                    toVec(w.transform(math::float3(B.x, B.y, B.z))),
				                    toVec(w.transform(math::float3(C.x, C.y, C.z))));
			}
		}
	}
	int sensors = 0;
	for (size_t i = 0; i < m_world.meshCount(); ++i) if (!m_world.mesh((int)i).solid) ++sensors;
	printf("collision: %d meshes (%d passable), %d triangles, %d trigger volumes\n",
	       (int)m_world.meshCount(), sensors, (int)m_world.triangleCount(), (int)m_volumes.size());
}

void BallController::addSphereTrigger(const std::string& trigger, hgr::Node* at, float radius) {
	Volume v;
	v.trigger = trigger;
	v.node = at;
	v.sphere = true;
	v.radius = radius;
	v.inside = false;
	m_volumes.push_back(v);
}

void BallController::setFormInfo(const std::string& csv) {
	std::vector<std::string> t;
	size_t a = 0;
	while (a <= csv.size()) {
		size_t b = csv.find(',', a);
		if (b == std::string::npos) b = csv.size();
		t.push_back(csv.substr(a, b - a));
		a = b + 1;
	}
	if (t.size() < 13) { fprintf(stderr, "D_forminfo incompleto (%d campos)\n", (int)t.size()); return; }
	BallParams& p = m_ball.params;
	m_shadowName = t[4];
	p.radius = (float)atof(t[5].c_str());
	p.mass = (float)atof(t[6].c_str());
	p.movementForce = (float)atof(t[7].c_str());
	p.jumpForce = (float)atof(t[8].c_str());
	p.friction = (float)atof(t[9].c_str());
	p.frictionSlide = (float)atof(t[10].c_str());
	p.maxSpeed = (float)atof(t[11].c_str());
	p.rebound = (float)atof(t[12].c_str());
	m_formDefaults = p;
	m_hasForm = true;
	publishBallValues(p);
	if (!m_sw->hasVariable("gravity")) m_sw->setVariable("gravity", 2000.f);
}

void BallController::publishBallValues(const BallParams& p) {
	m_sw->setVariable("ballmass", p.mass);
	m_sw->setVariable("ballrebound", p.rebound);
	m_sw->setVariable("ballmaxspeed", p.maxSpeed);
	m_sw->setVariable("ballfriction", p.friction);
	m_sw->setVariable("ballfrictionslide", p.frictionSlide);
	m_sw->setVariable("balljumpforce", p.jumpForce);
	m_sw->setVariable("ballmovementforce", p.movementForce);
}

void BallController::readBallValues() {
	BallParams& p = m_ball.params;
	p.mass = m_sw->getVariable("ballmass");
	p.rebound = m_sw->getVariable("ballrebound");
	p.friction = m_sw->getVariable("ballfriction");
	p.maxSpeed = m_sw->getVariable("ballmaxspeed");
	p.frictionSlide = m_sw->getVariable("ballfrictionslide");
	p.jumpForce = m_sw->getVariable("balljumpforce");
	p.movementForce = m_sw->getVariable("ballmovementforce");
}

void BallController::setBallModel(hgr::Node* model) {
	m_model = model;
	model->setTransform(math::float3x4(1.f));
	float best = 0.f;
	math::float3 center(0, 0, 0);
	std::string bestName;
	const bool trace = getenv("REDBOUND_TRACE") != 0;
	for (hgr::Node* n = model; n; n = n->next(model)) {
		if (n->classId() != hgr::Node::NODE_MESH) continue;
		hgr::Visual* v = static_cast<hgr::Visual*>(n);
		const math::float3 mn = v->boundBoxMin(), mx = v->boundBoxMax();
		const float ex = mx.x - mn.x, ey = mx.y - mn.y, ez = mx.z - mn.z;
		if (!(ex > 0.f && ey > 0.f && ez > 0.f) || ex > 5000.f || ey > 5000.f || ez > 5000.f) continue;
		const float lo = std::min(ex, std::min(ey, ez)), hi = std::max(ex, std::max(ey, ez));
		const bool spherical = lo / hi > 0.75f;
		if (trace) printf("[trace] model: %-24s size %.1f x %.1f x %.1f%s\n", n->name().c_str(), ex, ey, ez, spherical ? "  (esferica)" : "");
		if (!spherical || hi * 0.5f <= best) continue;
		best = hi * 0.5f;
		bestName = n->name().c_str();
		center = n->worldTransform().transform((mn + mx) * 0.5f);
	}
	if (best > 1.f && !m_hasForm) m_ball.params.radius = best;
	m_modelOffset = Vec3(center.x, center.y, center.z);
	m_bodyName = bestName;
	for (hgr::Node* n = model; n; n = n->next(model)) {
		std::string low = n->name().c_str();
		std::transform(low.begin(), low.end(), low.begin(), ::tolower);
		if (n->name() == bestName.c_str() && n->classId() == hgr::Node::NODE_MESH) { m_body = n; m_bodyRest = n->transform(); }
		if (low.find("camera") != std::string::npos || low == "splash") n->setEnabled(false);
	}
	hgr::Node* rolling = model->findNodeByNameFromSubtree("G_bounce_eyes_rolling");
	if (rolling) rolling->unlink();
	m_camPosNode = model->findNodeByNameFromSubtree("cameraposition");
	m_camLookNode = model->findNodeByNameFromSubtree("cameralookat");
	m_eyes = model->findNodeByNameFromSubtree("G_bounce_lookat_eyes");
	if (m_eyes) {
		m_eyesRest = m_eyes->transform();
		hgr::Node* balls = m_eyes->findNodeByNameFromSubtree("bounce_eyeballs");
		if (m_eyes->parent()) {
			const math::float3x4 pinv = m_eyes->parent()->worldTransform().inverse();
			m_ballCenterLocal = pinv.transform(math::float3(m_modelOffset.x, m_modelOffset.y, m_modelOffset.z));
			if (balls && balls->classId() == hgr::Node::NODE_MESH) {
				hgr::Visual* v = static_cast<hgr::Visual*>(balls);
				const math::float3 c = balls->worldTransform().transform((v->boundBoxMin() + v->boundBoxMax()) * 0.5f);
				math::float3 f = pinv.transform(c) - m_ballCenterLocal;
				m_eyeDistLocal = f.length();
				if (f.length() > 1e-3f) m_faceLocal = f * (1.f / f.length());
			}
		}
	}
	if (m_eyes) printf("eyes: %.1f from the center (eye group space); lowered by 12 %% of that\n", m_eyeDistLocal);
	if (getenv("REDBOUND_TRACE"))
		printf("[trace] model: camera %s, eyes %s\n", m_camPosNode && m_camLookNode ? "cameraposition/cameralookat" : "(no)", m_eyes ? "G_bounce_lookat_eyes" : "(no)");
	hgr::Node* sh = model->findNodeByNameFromSubtree(m_shadowName.c_str());
	if (sh) {
		P(hgr::Node) keep = sh;
		m_shadowRest = sh->worldTransform();
		m_shadowRest.setTranslation(math::float3(0, 0, 0));
		sh->unlink();
		sh->linkTo(m_scene);
		m_shadow = sh;
		m_ownShadow = false;
	}
}

void BallController::createShadow(gr::Context* ctx) {
	if (m_shadow) return;
	try {
		gr::VertexFormat vf;
		vf.addPosition().addTextureCoordinate(gr::VertexFormat::DF_V2_32);
		P(gr::Primitive) prim = ctx->createPrimitive(gr::Primitive::PRIM_TRI, vf, 4, 6);
		{
			gr::Primitive::Lock lk(prim, gr::Primitive::LOCK_WRITE);
			const math::float4 pos[4] = { math::float4(-1, 0, -1, 1), math::float4(1, 0, -1, 1), math::float4(1, 0, 1, 1), math::float4(-1, 0, 1, 1) };
			const math::float4 uv[4] = { math::float4(0, 0, 0, 0), math::float4(1, 0, 0, 0), math::float4(1, 1, 0, 0), math::float4(0, 1, 0, 0) };
			const int idx[6] = { 0, 2, 1, 0, 3, 2 };
			prim->setVertexPositions(0, pos, 4);
			prim->setVertexTextureCoordinates(0, 0, uv, 4);
			prim->setIndices(0, idx, 6);
		}
		P(gr::Shader) fx = ctx->createShader("unlit-tex-alpha", 0);
		P(gr::Texture) tex = ctx->createTexture("ball_shadow.pvr");
		fx->setTexture("BASEMAP", tex);
		prim->setShader(fx);
		P(hgr::Mesh) mesh = new hgr::Mesh;
		mesh->setName("sombra_jugador");
		mesh->addPrimitive(prim);
		mesh->setBoundBox(math::float3(-1, -0.1f, -1), math::float3(1, 0.1f, 1));
		mesh->linkTo(m_scene);
		m_shadow = mesh.ptr();
		m_ownShadow = true;
	} catch (lang::Throwable&) {
		fprintf(stderr, "could not create the ball shadow\n");
	}
}

void BallController::updateShadow() {
	if (!m_shadow) return;
	const float r = m_ball.params.radius;
	Vec3 ground, gn;
	if (m_rail >= 0) {
		const Vec3 down = m_ball.up * -1.f;
		if (!m_world.ray(m_ball.position, down, r * 12.f, ground, gn)) { m_shadow->setEnabled(false); return; }
	} else {
		float groundY;
		if (!m_world.rayDown(m_ball.position, r * 12.f, groundY, gn)) { m_shadow->setEnabled(false); return; }
		ground = Vec3(m_ball.position.x, groundY, m_ball.position.z);
	}
	const float height = std::max(0.f, (m_ball.position - ground).length() - r);
	const float maxHeight = r * 6.f;
	if (height > maxHeight) { m_shadow->setEnabled(false); return; }
	m_shadow->setEnabled(true);
	const float scale = (m_ownShadow ? r * 0.95f : 1.f) * (1.f - 0.6f * height / maxHeight);
	math::float3x4 tm(1.f);
	if (m_ownShadow) {
		tm(0, 0) = scale;
		tm(2, 2) = scale;
	} else {
		math::float3x4 yaw(1.f);
		yaw.setRotation(math::float3x3(math::float3(0, 1, 0), m_yaw));
		math::float3x4 sc(1.f);
		sc(0, 0) = scale;
		sc(2, 2) = scale;
		tm = sc * yaw * m_shadowRest;
	}
	const Vec3 up(0, 1, 0);
	const Vec3 axis = up.cross(gn);
	const float sn = axis.length(), cs = up.dot(gn);
	if (sn > 1e-4f) {
		math::float3x4 tilt(1.f);
		tilt.setRotation(math::float3x3(math::float3(axis.x / sn, axis.y / sn, axis.z / sn), atan2f(sn, cs)));
		tm = tilt * tm;
	} else if (cs < 0.f) {
		math::float3x4 flip(1.f);
		flip.setRotation(math::float3x3(math::float3(1, 0, 0), 3.14159265f));
		tm = flip * tm;
	}
	const Vec3 at = ground + gn * (r * 0.12f);
	tm.setTranslation(math::float3(at.x, at.y, at.z));
	m_shadow->setTransform(tm);
}

void BallController::addObjectShadow(hgr::Node* shadow, hgr::Node* owner, bool onlyNearGround) {
	ObjShadow o;
	o.shadow = shadow;
	o.owner = owner;
	o.onlyNearGround = onlyNearGround;
	o.rest = shadow->worldTransform();
	o.rest.setTranslation(math::float3(0, 0, 0));
	P(hgr::Node) keep = shadow;
	shadow->unlink();
	shadow->linkTo(m_scene);
	m_objShadows.push_back(o);
}

void BallController::updateObjectShadows() {
	for (ObjShadow& o : m_objShadows) {
		bool linked = false;
		for (hgr::Node* p = o.owner; p; p = p->parent()) if (p == (hgr::Node*)m_scene) { linked = true; break; }
		const math::float3x4 ow = o.owner->worldTransform();
		const math::float3 op = ow.translation();
		float gy;
		Vec3 gn;
		if (!linked || !o.owner->enabled() || !m_world.rayDown(Vec3(op.x, op.y + 20.f, op.z), 2000.f, gy, gn)) {
			o.shadow->setEnabled(false);
			continue;
		}
		if (o.onlyNearGround && op.y - gy > 120.f) {
			o.shadow->setEnabled(false);
			continue;
		}
		o.shadow->setEnabled(true);
		math::float3x4 tm = o.rest;
		math::float3 c(0, 0, 0);
		if (o.shadow->classId() == hgr::Node::NODE_MESH) {
			hgr::Visual* v = static_cast<hgr::Visual*>((hgr::Node*)o.shadow);
			c = (v->boundBoxMin() + v->boundBoxMax()) * 0.5f;
		}
		const Vec3 up(0, 1, 0);
		const Vec3 axis = up.cross(gn);
		const float sn = axis.length(), cs = up.dot(gn);
		if (sn > 1e-4f) {
			math::float3x4 tilt(1.f);
			tilt.setRotation(math::float3x3(math::float3(axis.x / sn, axis.y / sn, axis.z / sn), atan2f(sn, cs)));
			tm = tilt * tm;
		}
		const math::float3 cw = tm.rotate(c);
		const Vec3 at = Vec3(op.x, gy, op.z) + gn * 4.f;
		tm.setTranslation(math::float3(at.x - cw.x, at.y - cw.y, at.z - cw.z));
		o.shadow->setTransform(tm);
		if (getenv("REDBOUND_TRACE") && !o.traced) {
			o.traced = true;
			printf("[trace] shadow '%s' of '%s' placed at (%.0f, %.0f, %.0f), ground y=%.0f\n", o.shadow->name().c_str(),
			       o.owner->name().c_str(), op.x - cw.x, gy + 2.f - cw.y, op.z - cw.z, gy);
		}
	}
}

void BallController::spawnAt(const std::string& nodeName) {
	hgr::Node* n = m_sw->findNode(nodeName);
	if (!n) { spawn(); return; }
	Vec3 p = toVec(n->worldTransform().translation());
	float groundY;
	if (m_world.rayDown(p + Vec3(0, 300, 0), 3000.f, groundY)) p.y = groundY + m_ball.params.radius + 5.f;
	m_ball.reset(p);
}

void BallController::spawn() {
	hgr::Node* start = 0;
	std::string why;
	for (hgr::Node* n = m_scene->firstChild(); n && !start; n = n->next(m_scene)) {
		std::string name = n->name().c_str();
		std::string low = name;
		std::transform(low.begin(), low.end(), low.begin(), ::tolower);
		if ((low.compare(0, 2, "d_") == 0 || low.compare(0, 2, "b_") == 0) && low.find("start") != std::string::npos) { start = n; why = name; }
	}
	for (hgr::Node* n = m_scene->firstChild(); n && !start; n = n->next(m_scene)) {
		std::string low = n->name().c_str();
		std::transform(low.begin(), low.end(), low.begin(), ::tolower);
		if (low.compare(0, 12, "d_checkpoint") == 0) { start = n; why = n->name().c_str(); }
	}
	Vec3 p;
	if (start) {
		p = toVec(start->worldTransform().translation());
	} else {
		hgr::Node* cam = m_scene->findNodeByName("O_startcamera");
		if (cam) {
			const math::float3x4 t = cam->worldTransform();
			p = toVec(t.translation() + t.getColumn(2) * 400.f);
			why = "in front of O_startcamera";
		}
	}
	float groundY;
	if (m_world.rayDown(p + Vec3(0, 200, 0), 3000.f, groundY)) p.y = groundY + m_ball.params.radius + 5.f;
	m_ball.reset(p);
	char buf[256];
	snprintf(buf, sizeof(buf), "ball: radius %.1f (body '%s'), mass %g, force %g, jump %g, friction %g, max speed %g, rebound %g, "
	         "gravity %g; start at %s (%.0f, %.0f, %.0f)", m_ball.params.radius, m_bodyName.c_str(), m_ball.params.mass,
	         m_ball.params.movementForce, m_ball.params.jumpForce, m_ball.params.friction, m_ball.params.maxSpeed,
	         m_ball.params.rebound, m_ball.params.gravity, why.c_str(), p.x, p.y, p.z);
	spawnInfo = buf;
}

bool BallController::insideVolume(const Volume& v, const Vec3& p) const {
	const math::float3x4 w = v.node->worldTransform();
	if (v.sphere) {
		const Vec3 c = toVec(w.translation());
		const float r = v.radius + m_ball.params.radius;
		return (p - c).dot(p - c) < r * r;
	}
	const math::float3 local = w.inverse().transform(math::float3(p.x, p.y, p.z));
	const float r = m_ball.params.radius;
	return local.x > v.boxMin.x - r && local.x < v.boxMax.x + r && local.y > v.boxMin.y - r && local.y < v.boxMax.y + r &&
	       local.z > v.boxMin.z - r && local.z < v.boxMax.z + r;
}

void BallController::checkVolumes() {
	for (Volume& v : m_volumes) {
		const bool in = insideVolume(v, m_ball.position);
		if (in && !v.inside && m_sw->triggerState(v.trigger) == Trigger::ENABLED) {
			if (getenv("REDBOUND_TRACE")) printf("[trace] ball enters %s\n", v.trigger.c_str());
			m_sw->activateTrigger(v.trigger);
		}
		v.inside = in;
	}
	for (int owner : m_ball.touched) {
		const std::string& t = m_world.mesh(owner).executeTrigger;
		if (!t.empty() && m_sw->triggerState(t) == Trigger::ENABLED) {
			if (getenv("REDBOUND_TRACE")) printf("[trace] ball touches %s -> %s\n", m_world.mesh(owner).name.c_str(), t.c_str());
			m_sw->activateTrigger(t);
		}
	}
}

void BallController::loadRails() {
	m_rails.clear();
	m_guides.clear();
	m_guide = -1;
	hgr::UserPropertySet* ups = m_scene->userProperties();
	for (hgr::Node* n = m_scene->firstChild(); n; n = n->next(m_scene)) {
		const std::string name = n->name().c_str();
		if (name.compare(0, 2, "L_") != 0) continue;
		hgr::Node* track = 0;
		hgr::Node* normal = 0;
		hgr::Node* camPos = 0;
		hgr::Node* camLook = 0;
		for (hgr::Node* c = n->firstChild(); c; c = c->next(n)) {
			if (c->parent() != n || c->classId() != hgr::Node::NODE_LINES) continue;
			const std::string child = c->name().c_str();
			if (child == "track") track = c;
			else if (child == "tracknormal") normal = c;
			else if (child == "cameraposition") camPos = c;
			else if (child == "cameralookat") camLook = c;
		}
		if (!track) continue;
		auto segments = [](hgr::Node* node) {
			std::vector<std::pair<Vec3, Vec3>> segs;
			hgr::Lines* l = static_cast<hgr::Lines*>(node);
			const math::float3x4 tm = node->worldTransform();
			for (int i = 0; i < l->lines(); ++i) {
				const math::float3 a = tm.transform(l->getLineStart(i)), b = tm.transform(l->getLineEnd(i));
				segs.push_back(std::make_pair(Vec3(a.x, a.y, a.z), Vec3(b.x, b.y, b.z)));
			}
			return segs;
		};
		if (camPos && camLook) {
			Guide g;
			g.name = name;
			g.track.build(RailTrack::chain(segments(track)), std::vector<Vec3>());
			g.position.build(RailTrack::chain(segments(camPos)), std::vector<Vec3>());
			g.lookAt.build(RailTrack::chain(segments(camLook)), std::vector<Vec3>());
			if (g.track.valid() && g.position.valid() && g.lookAt.valid()) {
				m_guides.push_back(g);
				printf("camera guide %s: track %.0f, camera path %.0f, look path %.0f\n", name.c_str(), g.track.length(), g.position.length(), g.lookAt.length());
			}
		}
		Rail r;
		r.name = name;
		std::vector<size_t> breaks;
		const std::vector<Vec3> tp = RailTrack::chain(segments(track), &breaks);
		const std::vector<Vec3> np = normal ? RailTrack::chain(segments(normal)) : std::vector<Vec3>();
		r.track.build(tp, np);
		r.track.setBreaks(breaks);
		if (getenv("REDBOUND_TRACE")) printf("[trace] rail %s: %d points, %d pieces, length %.0f\n", name.c_str(), (int)tp.size(), (int)breaks.size() + 1, r.track.length());
		const std::string dname = "D_" + name.substr(2);
		const std::string props = ups && ups->containsKey(dname.c_str()) ? ups->get(dname.c_str()).c_str() : "";
		if (!prop(props, "force").empty()) r.force = (float)atof(prop(props, "force").c_str());
		if (!prop(props, "forwarddirection").empty()) r.forwardDir = atof(prop(props, "forwarddirection").c_str()) < 0 ? -1.f : 1.f;
		r.deathplane = prop(props, "deathplane");
		r.next = prop(props, "next");
		if (prop(props, "tracknormalgravity") != "1") continue;
		if (r.track.valid()) m_rails.push_back(r);
	}
	if (!m_rails.empty()) printf("rails: %d (total length %.0f)\n", (int)m_rails.size(), [&] { float t = 0; for (auto& r : m_rails) t += r.track.length(); return t; }());
}

int BallController::findRail(const std::string& name) const {
	for (size_t i = 0; i < m_rails.size(); ++i) if (m_rails[i].name == name) return (int)i;
	return -1;
}

void BallController::processRequests() {
	std::vector<SceneWorld::BallRequest> reqs = m_sw->takeBallRequests();
	for (const SceneWorld::BallRequest& r : reqs) {
		switch (r.kind) {
		case SceneWorld::BallRequest::ATTACH_RAIL: {
			const int i = findRail(r.name);
			if (i >= 0) { m_rail = i; m_railS = m_rails[i].track.closest(m_ball.position).s; }
			break;
		}
		case SceneWorld::BallRequest::SET_GUIDE: {
			m_guide = -1;
			for (size_t i = 0; i < m_guides.size(); ++i)
				if (m_guides[i].name == r.name) {
					m_guide = (int)i;
					m_guideS = m_guides[i].track.closest(m_ball.position).s;
				}
			printf("camera guide: %s\n", m_guide >= 0 ? (r.name + " active").c_str() : ("off (" + r.name + ")").c_str());
			break;
		}
		case SceneWorld::BallRequest::DETACH_RAIL:
			if (m_rail >= 0 && m_rails[m_rail].name == r.name) { m_rail = -1; m_ball.up = Vec3(0, 1, 0); }
			break;
		case SceneWorld::BallRequest::SPEED_MUL:
			m_ball.velocity = m_ball.velocity * r.value;
			if (r.value > 1.f && m_rail >= 0) {
				const float speedScale = 1.15f;
				const float sp = m_ball.velocity.length(), cap = m_railMaxSpeed * speedScale;
				if (sp > cap) m_ball.velocity = m_ball.velocity * (cap / sp);
			}
			break;
		case SceneWorld::BallRequest::MAX_SPEED:
			m_railMaxSpeed = r.value;
			break;
		case SceneWorld::BallRequest::SET_POS: {
			hgr::Node* n = m_scene->findNodeByName(r.name.c_str());
			const size_t at = r.name.rfind("_L_");
			if (n) {
				const math::float3 p = n->worldTransform().translation();
				m_ball.position = Vec3(p.x, p.y, p.z);
			} else if (at != std::string::npos) {
				const int i = findRail(r.name.substr(at + 1));
				if (i >= 0) {
					const RailSample s0 = m_rails[i].track.at(0.f);
					m_ball.position = s0.pos + s0.up * (m_ball.params.radius * 1.5f);
					m_rail = i;
					m_railS = 0.f;
				}
			}
			m_ball.velocity = Vec3(0, 0, 0);
			m_railDead = false;
			m_railAirTime = 0.f;
			break;
		}
		}
	}
}

void BallController::railInput(float turn, BallInput& in) {
	Rail& rail = m_rails[m_rail];
	RailSample s = rail.track.closestNear(m_ball.position, m_railS, 600.f, 3000.f);
	if (s.distance > 1500.f) {
		const RailSample g = rail.track.closest(m_ball.position);
		if (g.distance < s.distance * 0.5f) {
			if (getenv("REDBOUND_TRACE")) printf("[trace] rail %s: resynced from s %.0f to %.0f (distance %.0f -> %.0f)\n", rail.name.c_str(), s.s, g.s, s.distance, g.distance);
			s = g;
			m_railS = g.s;
		}
	}
	if (s.s >= rail.track.length() - 1.f && !rail.next.empty()) {
		const int n = findRail(rail.next);
		if (n >= 0) {
			if (!m_sw->levelCompleted()) m_railDistance += rail.track.length() - m_railS;
			m_rail = n;
			m_railS = 0.f;
			s = m_rails[n].track.closestNear(m_ball.position, 0.f, 0.f, 3000.f);
		}
	}
	Rail& cur = m_rails[m_rail];
	const bool counting = !m_sw->levelCompleted();
	if (counting && s.s > m_railS) m_railDistance += s.s - m_railS;
	m_railS = s.s;
	m_railSample = s;
	m_ball.up = m_ball.up + (s.up - m_ball.up) * 0.2f;
	const Vec3 fwd = s.forward * cur.forwardDir;
	const Vec3 right = m_ball.up.cross(fwd);
	const bool controls = !m_sw->hasVariable("controls") || m_sw->getVariable("controls") != 0.f;
	Vec3& v = m_ball.velocity;
	const float vf = v.dot(fwd);
	const float speedScale = 1.15f;
	const float target = m_railMaxSpeed * speedScale;
	const float rate = vf < target ? 2.5f : 0.7f;
	const float nf = vf + (target - vf) * std::min(1.f, m_railDt * rate);
	v = v + fwd * (nf - vf);
	const float vl = v.dot(right);
	if (!controls || std::fabs(turn) < 0.01f) v = v - right * (vl * std::min(1.f, m_railDt * 6.f));
	in.move = right * (controls ? turn * 3.f : 0.f);
	m_ball.params.friction = 0.f;
	m_ball.params.maxSpeed = target * 1.1f;
	if (!m_ball.onGround) {
		const float vu = v.dot(m_ball.up);
		if (vu > 0.f) v = v - m_ball.up * (vu * std::min(1.f, m_railDt * 10.f));
	}
	m_heading = fwd;
	m_railAirTime = m_ball.onGround ? 0.f : m_railAirTime + m_railDt;
	const bool fell = (m_railAirTime > 0.7f && s.distance > 1200.f) || s.distance > 6000.f;
	if (fell && !m_railDead && !cur.deathplane.empty()) {
		m_railDead = true;
		if (getenv("REDBOUND_TRACE")) printf("[trace] rail %s: fell (s %.0f, distance %.0f, %.1f s airborne), %s\n", cur.name.c_str(), s.s, s.distance, m_railAirTime, cur.deathplane.c_str());
		m_sw->activateTrigger(cur.deathplane);
	}
	if (++m_trailTick % 10 == 0) {
		m_trail.push_back(m_ball.position);
		m_trailS.push_back(s.s);
		if (m_trail.size() > 12) { m_trail.erase(m_trail.begin()); m_trailS.erase(m_trailS.begin()); }
	}
	m_sw->setVariable("speed01", v.length());
}

void BallController::update(float dt, float forward, float turn, bool jump) {
	m_railDt = dt;
	processRequests();
	const std::string& cam = m_sw->activeCamera();
	m_cinematic = !cam.empty() && cam != "camera";
	if (m_cinematic) {
		forward = 0.f; turn = 0.f; jump = false;
		m_ball.velocity.x = 0.f;
		m_ball.velocity.z = 0.f;
		hgr::Node* cn = m_scene->findNodeByName(cam.c_str());
		if (cn) {
			const math::float3 cp = cn->worldTransform().translation();
			m_cinematicCam = Vec3(cp.x, cp.y, cp.z);
		}
	}
	if (m_rail < 0) m_yaw += turn * 2.4f * dt;
	if (m_sw->hasVariable("gravity")) m_ball.params.gravity = m_sw->getVariable("gravity");
	if (m_sw->takeResetBallValues() && m_hasForm) publishBallValues(m_formDefaults);
	if (m_sw->takeUpdateBallValues()) readBallValues();
	BallInput in;
	in.jump = jump;
	if (m_modernReq != m_modern) {
		if (m_modernReq) m_camYaw = m_yaw;
		else m_yaw = m_camYaw;
		m_modern = m_modernReq;
	}
	if (m_modern) {
		m_camYaw += m_dCamYaw;
		m_camPitch = std::max(-0.35f, std::min(1.2f, m_camPitch + m_dCamPitch));
		if (m_followPath && !m_cinematic && m_rail < 0) {
			const Vec3 hv(m_ball.velocity.x, 0.f, m_ball.velocity.z);
			const float sp = hv.length();
			const Vec3 cf(sinf(m_camYaw), 0, cosf(m_camYaw));
			const float fwdComp = hv.dot(cf);
			if (sp > 150.f && fwdComp > 0.7f * sp) {
				float d = atan2f(hv.x, hv.z) - m_camYaw;
				while (d > 3.14159265f) d -= 6.2831853f;
				while (d < -3.14159265f) d += 6.2831853f;
				const float rate = 1.6f * std::min(1.f, fwdComp / 600.f) * (1.f - 0.75f * std::min(1.f, std::fabs(m_mx)));
				const float step = d * std::min(1.f, dt * rate), maxStep = 0.8f * dt;
				m_camYaw += std::max(-maxStep, std::min(maxStep, step));
			}
		}
		m_autoFollowBlend += ((m_autoFollow ? 1.f : 0.f) - m_autoFollowBlend) * std::min(1.f, dt * 2.f);
		if (m_autoFollowBlend > 0.01f && !m_cinematic && m_rail < 0) {
			const Vec3 hv(m_ball.velocity.x, 0.f, m_ball.velocity.z);
			const float sp = hv.length();
			const Vec3 cf(sinf(m_camYaw), 0, cosf(m_camYaw));
			if (sp > 120.f && hv.dot(cf) > -0.3f * sp) {
				float d = atan2f(hv.x, hv.z) - m_camYaw;
				while (d > 3.14159265f) d -= 6.2831853f;
				while (d < -3.14159265f) d += 6.2831853f;
				const float rate = 1.4f * m_autoFollowBlend * std::min(1.f, sp / 500.f);
				m_camYaw += std::max(-1.2f * dt, std::min(1.2f * dt, d * std::min(1.f, dt * rate)));
			}
		}
	}
	m_dCamYaw = m_dCamPitch = 0.f;
	Vec3 heading(sinf(m_yaw), 0, cosf(m_yaw));
	if (m_rail >= 0) {
		railInput(m_modern ? m_mx : turn, in);
		heading = m_heading;
		m_yaw = atan2f(heading.x, heading.z);
	} else if (m_modern && !m_cinematic) {
		const Vec3 cf(sinf(m_camYaw), 0, cosf(m_camYaw)), cr(cosf(m_camYaw), 0, -sinf(m_camYaw));
		Vec3 dir = cr * m_mx + cf * m_my;
		const float l = dir.length();
		if (l > 1.f) dir = dir * (1.f / l);
		in.move = dir;
		if (l > 0.15f) {
			const float target = atan2f(dir.x, dir.z);
			float d = target - m_yaw;
			while (d > 3.14159265f) d -= 6.2831853f;
			while (d < -3.14159265f) d += 6.2831853f;
			m_yaw += d * std::min(1.f, dt * 8.f);
		}
		heading = Vec3(sinf(m_yaw), 0, cosf(m_yaw));
	} else {
		in.move = heading * forward;
	}
	m_ball.update(dt, in, m_world);
	m_heading = heading;
	if (m_rail >= 0 && m_ball.impactSpeed > 250.f && std::fabs(m_ball.impactNormal.dot(m_ball.up)) < 0.5f) {
		const Vec3 f = m_heading;
		const float vf = m_ball.velocity.dot(f);
		if (vf > 400.f) m_ball.velocity = m_ball.velocity - f * (vf - std::max(300.f, vf * 0.15f));
	}
	if (playSound) {
		if (m_ball.jumped) playSound("bounce_jump.wav", 1.f);
		m_impactCooldown -= dt;
		const float minImpact = m_rail >= 0 ? 1200.f : 350.f, gap = m_rail >= 0 ? 0.6f : 0.15f;
		if (m_ball.impactSpeed > minImpact && m_impactCooldown <= 0.f && !m_ball.jumped) {
			playSound("bounce_collision.wav", std::min(1.f, m_ball.impactSpeed / (m_rail >= 0 ? 4000.f : 1400.f)));
			m_impactCooldown = gap;
		}
	}

	if (m_sw->takeRespawn()) {
		if (!m_sw->checkpoint().empty()) spawnAt(m_sw->checkpoint());
		else spawn();
	}
	if (m_rail < 0 && m_ball.position.y < m_ball.params.killY) {
		if (!m_sw->checkpoint().empty()) spawnAt(m_sw->checkpoint());
		else spawn();
	}
	checkVolumes();

	updateShadow();
	updateObjectShadows();
	m_eyeDt = dt;
	updateEyes();

	m_roll += m_ball.velocity.dot(heading) * dt / m_ball.params.radius;

	if (m_model) {
		math::float3x4 tm(1.f);
		tm.setRotation(math::float3x3(math::float3(0, 1, 0), m_yaw));
		const math::float3 off = math::float3x3(math::float3(0, 1, 0), m_yaw) * math::float3(m_modelOffset.x, m_modelOffset.y, m_modelOffset.z);
		tm.setTranslation(math::float3(m_ball.position.x, m_ball.position.y, m_ball.position.z) - off);
		m_model->setTransform(tm);
		if (m_body) {
			const math::float3 c(m_modelOffset.x, m_modelOffset.y, m_modelOffset.z);
			math::float3x4 toC(1.f), back(1.f), rot(1.f);
			toC.setTranslation(-c);
			back.setTranslation(c);
			rot.setRotation(math::float3x3(math::float3(1, 0, 0), m_roll));
			m_body->setTransform(back * rot * toC * m_bodyRest);
		}
	}
}

static math::float3x3 lookBasis(math::float3 f, math::float3 up) {
	f *= 1.f / f.length();
	math::float3 r(up.y * f.z - up.z * f.y, up.z * f.x - up.x * f.z, up.x * f.y - up.y * f.x);
	if (r.length() < 1e-5f) r = math::float3(1, 0, 0);
	r *= 1.f / r.length();
	const math::float3 u(f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x);
	math::float3x3 m;
	m.setColumn(0, r);
	m.setColumn(1, u);
	m.setColumn(2, f);
	return m;
}

bool BallController::controllable() const {
	if (m_cinematic || !m_sw) return false;
	if (m_sw->hasVariable("controls") && m_sw->getVariable("controls") == 0.f) return false;
	if (m_sw->hasVariable("countdown") && m_sw->getVariable("countdown") > 0.f) return false;
	return true;
}

void BallController::updateEyes() {
	if (!m_eyes || !m_eyes->parent()) return;
	Vec3 want;
	if (m_cinematic) want = m_cinematicCam - m_ball.position;
	else {
		const Vec3 hv(m_ball.velocity.x, 0.f, m_ball.velocity.z);
		if (hv.length() > 40.f) want = hv;
	}
	want.y = 0.f;
	if (want.length() > 1e-3f) {
		want = want * (1.f / want.length());
		const float t = std::min(1.f, m_eyeDt * 10.f);
		m_eyeDir = m_eyeDir + (want - m_eyeDir) * t;
		if (m_eyeDir.length() < 1e-3f) m_eyeDir = want;
		m_eyeDir = m_eyeDir * (1.f / m_eyeDir.length());
	}
	if (m_eyeDir.length() < 1e-3f) m_eyeDir = m_heading;
	const math::float3x4 parentInv = m_eyes->parent()->worldTransform().inverse();
	math::float3 d = parentInv.rotate(math::float3(m_eyeDir.x, 0.f, m_eyeDir.z));
	const math::float3 upL = parentInv.rotate(math::float3(0, 1, 0));
	const float restPitch = asinf(std::max(-1.f, std::min(1.f, m_faceLocal.x * upL.x + m_faceLocal.y * upL.y + m_faceLocal.z * upL.z)));
	const float dl = d.length();
	if (dl < 1e-4f) return;
	d *= 1.f / dl;
	float pitch = asinf(std::max(-1.f, std::min(1.f, d.x * upL.x + d.y * upL.y + d.z * upL.z)));
	const float maxTilt = 0.f;
	pitch = std::max(restPitch - maxTilt, std::min(restPitch + maxTilt, pitch));
	math::float3 h = d - upL * (d.x * upL.x + d.y * upL.y + d.z * upL.z);
	if (h.length() < 1e-4f) return;
	h *= 1.f / h.length();
	const math::float3 target = h * cosf(pitch) + upL * sinf(pitch);
	const math::float3x3 from = lookBasis(m_faceLocal, upL), to = lookBasis(target, upL);
	math::float3x3 fromT;
	for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) fromT(r, c) = from(c, r);
	math::float3x4 rot(1.f);
	rot.setRotation(to * fromT);
	math::float3x4 toC(1.f), back(1.f);
	toC.setTranslation(-m_ballCenterLocal);
	back.setTranslation(m_ballCenterLocal);
	const float frac = 0.12f;
	math::float3x4 eyesTm = back * rot * toC * m_eyesRest;
	const float ul = upL.length();
	if (ul > 1e-6f && m_eyeDistLocal > 0.f) eyesTm.setTranslation(eyesTm.translation() - upL * (frac * m_eyeDistLocal / ul));
	m_eyes->setTransform(eyesTm);
}

void BallController::updateChaseCamera(hgr::Camera* cam, float dt) {
	if (m_rail >= 0) {
		float h = 158.f, d = 465.f;
		const Vec3 up = m_ball.up, fwd = m_heading;
		const Vec3 desired = m_ball.position - fwd * d + up * h;
		const Vec3 look = m_ball.position + fwd * 312.f + up * 15.f;
		if (!m_camInit) { m_camPos = desired; m_camLook = look; m_camUp = up; m_camInit = true; }
		m_camPos += (desired - m_camPos) * (1.f - expf(-dt * 10.f));
		m_camLook += (look - m_camLook) * (1.f - expf(-dt * 14.f));
		m_camUp += (up - m_camUp) * (1.f - expf(-dt * 6.f));
		cam->setPosition(math::float3(m_camPos.x, m_camPos.y, m_camPos.z));
		Vec3 up2 = m_camUp;
		const Vec3 view = m_camLook - m_camPos;
		if (view.length() > 1e-3f) up2 = up2 - view * (up2.dot(view) / view.dot(view));
		if (up2.length() < 1e-3f) up2 = Vec3(0, 1, 0);
		up2 = up2 * (1.f / up2.length());
		m_camUp = up2;
		cam->lookAt(math::float3(m_camLook.x, m_camLook.y, m_camLook.z), math::float3(up2.x, up2.y, up2.z));
		m_lastCam = m_camPos;
		m_haveCam = true;
		return;
	}
	const float r = m_ball.params.radius;
	Vec3 target = m_ball.position + Vec3(0, r * 0.5f, 0);
	Vec3 desired = m_ball.position - m_heading * (r * 12.f) + Vec3(0, r * 8.f, 0);
	const bool guided = m_useGuide && m_guide >= 0 && m_rail < 0;
	if (guided) {
		const Guide& g = m_guides[m_guide];
		const RailSample s = g.track.closestNear(m_ball.position, m_guideS, g.track.length() * 0.1f, g.track.length() * 0.1f);
		m_guideS = s.s;
		const float fraction = g.track.length() > 0.f ? s.s / g.track.length() : 0.f;
		desired = g.position.at(fraction * g.position.length()).pos;
		target = g.lookAt.at(fraction * g.lookAt.length()).pos;
		const Vec3 view = target - desired;
		if (Vec3(view.x, 0.f, view.z).length() > 1e-3f) m_camYaw = atan2f(view.x, view.z);
	} else if (m_modern) {
		const float dist = r * 14.4f;
		const float minElevation = 0.12f;
		const float elevation = std::max(minElevation, m_camPitch);
		const Vec3 back(-sinf(m_camYaw) * cosf(elevation), sinf(elevation), -cosf(m_camYaw) * cosf(elevation));
		desired = m_ball.position + back * dist;
		target = target + Vec3(0.f, (minElevation - std::min(minElevation, m_camPitch)) * dist * 1.5f, 0.f);
	}
	if (!m_camInit) { m_camPos = desired; m_camLook = target; m_camInit = true; }
	const float k = 1.f - expf(-dt * (guided ? 4.f : m_modern ? 9.f : 3.f));
	m_camPos += (desired - m_camPos) * k;
	m_camLook += (target - m_camLook) * (1.f - expf(-dt * 8.f));
	cam->setPosition(math::float3(m_camPos.x, m_camPos.y, m_camPos.z));
	cam->lookAt(math::float3(m_camLook.x, m_camLook.y, m_camLook.z));
	m_lastCam = m_camPos;
	m_haveCam = true;
}

}
