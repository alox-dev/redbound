#include "SceneWorld.h"
#include <hgr/TransformAnimationSet.h>
#include <hgr/UserPropertySet.h>
#include <lang/Hashtable.h>
#include <math/float3x4.h>
#include <math/float3x3.h>
#include <hgr/Mesh.h>
#include <gr/Primitive.h>
#include <gr/Shader.h>
#include <math/float4.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace redbound {

SceneWorld::SceneWorld(hgr::Scene* scene) : m_scene(scene) {
	m_nodes["scene"] = scene;
	for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene))
		m_nodes[n->name().c_str()] = n;

	hgr::UserPropertySet* ups = scene->userProperties();
	if (ups) {
		for (lang::HashtableIterator<lang::String, lang::String> it = ups->begin(); it != ups->end(); ++it) {
			const std::string props = it.value().c_str();
			if (props.find("a01") == std::string::npos && props.find("e01") == std::string::npos) continue;
			std::unique_ptr<Trigger> t(new Trigger(it.key().c_str()));
			std::vector<std::string> w = t->parse(props);
			m_warnings.insert(m_warnings.end(), w.begin(), w.end());
			m_triggers[it.key().c_str()] = std::move(t);
		}
	}
}

std::vector<std::string> SceneWorld::addTrigger(const std::string& name, const std::string& properties) {
	std::unique_ptr<Trigger> t(new Trigger(name));
	std::vector<std::string> w = t->parse(properties);
	m_triggers[name] = std::move(t);
	return w;
}

int SceneWorld::triggerState(const std::string& name) const {
	std::map<std::string, std::unique_ptr<Trigger>>::const_iterator it = m_triggers.find(name);
	return it == m_triggers.end() ? -1 : (int)it->second->state();
}

hgr::Node* SceneWorld::node(const std::string& name) const {
	std::map<std::string, P(hgr::Node)>::const_iterator it = m_nodes.find(name);
	return it == m_nodes.end() ? 0 : (hgr::Node*)it->second;
}

float SceneWorld::getVariable(const std::string& name) const {
	std::map<std::string, float>::const_iterator it = m_vars.find(name);
	return it == m_vars.end() ? 0.f : it->second;
}

void SceneWorld::setTriggerEnabled(const std::string& trigger, bool enabled) {
	std::map<std::string, std::unique_ptr<Trigger>>::iterator it = m_triggers.find(trigger);
	if (it == m_triggers.end()) return;
	Trigger& t = *it->second;
	const bool self = trigger == m_currentTrigger && (t.state() == Trigger::RUNNING || t.state() == Trigger::EXITING);
	if (enabled) {
		if (self) t.finishThenSet(Trigger::ENABLED);
		else if (t.state() == Trigger::DISABLED) t.setState(Trigger::ENABLED);
	} else {
		if (self) t.finishThenSet(Trigger::DISABLED);
		else t.setState(Trigger::DISABLED);
	}
}

void SceneWorld::activateTrigger(const std::string& trigger) {
	std::map<std::string, std::unique_ptr<Trigger>>::iterator it = m_triggers.find(trigger);
	if (it == m_triggers.end()) return;
	Trigger& t = *it->second;
	if (t.state() == Trigger::RUNNING || t.state() == Trigger::EXITING) return;
	t.setState(Trigger::RUNNING);
	if (m_activateDepth >= 8) return;
	++m_activateDepth;
	const std::string caller = m_currentTrigger;
	m_currentTrigger = trigger;
	t.update(*this, 0.f);
	m_currentTrigger = caller;
	--m_activateDepth;
}

void SceneWorld::link(const std::string& parent, const std::vector<std::string>& nodes) {
	hgr::Node* p = node(parent);
	if (!p) { fprintf(stderr, "[world] link: parent '%s' does not exist\n", parent.c_str()); return; }
	for (const std::string& n : nodes) {
		hgr::Node* c = node(n);
		if (!c) { fprintf(stderr, "[world] link: '%s' does not exist\n", n.c_str()); continue; }
		if (c->parent() != p) c->linkTo(p);
	}
}

void SceneWorld::unlink(const std::vector<std::string>& nodes) {
	static const bool trace = getenv("REDBOUND_TRACE") != 0;
	static const bool originalSections = getenv("REDBOUND_ORIGINAL_SECTIONS") != 0;
	for (const std::string& n : nodes) {
		hgr::Node* c = node(n == "this" ? std::string() : n);
		if (!c || !c->parent()) continue;
		if (trace && m_unhandled.insert("unlink " + n).second) {
			std::string meshes;
			int count = 0;
			for (hgr::Node* k = c; k; k = k->next(c))
				if (k->classId() == hgr::Node::NODE_MESH) { if (count < 12) meshes += std::string(count ? ", " : "") + k->name().c_str(); ++count; }
			printf("[trace] unlink %s removes %d meshes: %s%s\n", n.c_str(), count, meshes.c_str(), count > 12 ? ", ..." : "");
		}
		if (!originalSections && n.size() == 3 && n.compare(0, 2, "N_") == 0) continue;
		if (!originalSections && n.compare(0, 7, "O_water") == 0) continue;
		if (!originalSections && (m_currentTrigger.compare(0, 7, "T_link_") == 0 || m_currentTrigger.compare(0, 9, "T_unlink_") == 0 ||
		                          m_currentTrigger.compare(0, 12, "T_checkpoint") == 0))
			continue;
		c->unlink();
	}
}

int SceneWorld::playAnimation(const std::string& nodeName, const std::string& anim, float start, float end, bool loop) {
	hgr::Node* n = node(nodeName);
	if (!n) return -1;
	std::vector<hgr::TransformAnimationSet*> sets(1, m_scene->transformAnimations());
	sets.insert(sets.end(), m_animationSets.begin(), m_animationSets.end());
	P(hgr::TransformAnimation)* a = 0;
	for (const std::string& name : { anim, nodeName })
		for (hgr::TransformAnimationSet* set : sets)
			if (!a && set && set->containsKey(name.c_str()) && set->get(name.c_str())) a = &set->get(name.c_str());
	if (!a) return -1;
	Playing p;
	p.node = n;
	p.anim = *a;
	p.start = start;
	p.end = end < 0.f ? (*a)->endTime() : end;
	p.time = start;
	p.loop = loop;
	p.done = false;
	const int id = m_nextAnim++;
	m_playing[id] = p;
	return id;
}

bool SceneWorld::animationDone(int id) const {
	std::map<int, Playing>::const_iterator it = m_playing.find(id);
	return it == m_playing.end() || it->second.done;
}

int SceneWorld::startTransform(TransformKind kind, const std::string& object, const std::string& subnode,
                               float x, float y, float z, float duration) {
	hgr::Node* obj = node(object);
	if (!obj) {
		if (m_unhandled.insert("no node " + object).second)
			fprintf(stderr, "[world] transform: object '%s' does not exist\n", object.c_str());
		return -1;
	}
	hgr::Node* target = obj;
	if (!subnode.empty() && subnode != object) {
		hgr::Node* sub = obj->findNodeByNameFromSubtree(subnode.c_str());
		if (sub) target = sub;
	}
	if (getenv("REDBOUND_TRACE") && m_unhandled.insert("trace " + object + "/" + subnode).second) {
		int meshes = 0, prims = 0;
		for (hgr::Node* n = target; n; n = n->next(target))
			if (n->classId() == hgr::Node::NODE_MESH) { ++meshes; prims += static_cast<hgr::Mesh*>(n)->primitives(); }
		printf("[trace] transform type %d on '%s' (subnode '%s' %s) vector (%g %g %g) in %g s; meshes %d, primitives %d\n",
		       (int)kind, object.c_str(), subnode.c_str(), target == obj ? "not found or same" : "found",
		       x, y, z, duration, meshes, prims);
	}
	if (duration <= 0.f) duration = 0.001f;
	for (std::map<int, Moving>::iterator it = m_moving.begin(); it != m_moving.end(); ++it)
		if (it->second.node == target && it->second.kind == kind && !it->second.done) { it->second.done = true; it->second.elapsed = it->second.duration + 100.f; }
	Moving m;
	m.kind = kind;
	m.node = target;
	m.rate = math::float3(x, y, z) * (1.f / duration);
	m.elapsed = 0.f;
	m.duration = duration;
	m.done = false;
	m.u = m.v = 0.f;
	if (kind == UV) {
		std::map<hgr::Node*, std::pair<float, float> >::const_iterator o = m_uvOffset.find(target);
		if (o != m_uvOffset.end()) { m.u = o->second.first; m.v = o->second.second; }
	}
	const int id = m_nextAnim++;
	m_moving[id] = m;
	return id;
}

bool SceneWorld::transformDone(int id) const {
	std::map<int, Moving>::const_iterator it = m_moving.find(id);
	return it == m_moving.end() || it->second.done;
}

static void setUvOffset(hgr::Node* root, float u, float v) {
	for (hgr::Node* n = root; n; n = n->next(root)) {
		if (n->classId() != hgr::Node::NODE_MESH) continue;
		hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
		for (int i = 0; i < mesh->primitives(); ++i) {
			gr::Shader* fx = mesh->getPrimitive(i)->shader();
			if (fx) fx->setVector("TEX0_OFFSET", math::float4(u, v, 0, 0));
		}
	}
}

float SceneWorld::resolve(const std::string& a) const {
	char* end = 0;
	const float v = strtof(a.c_str(), &end);
	if (end && end != a.c_str() && *end == 0) return v;
	return getVariable(a);
}

bool SceneWorld::genericAction(const std::string& name, const std::vector<std::string>& args) {
	if (name == "setcamera") {
		if (!args.empty()) m_activeCamera = args[0];
		return true;
	}
	if (name == "checkpoint") {
		if (!args.empty()) m_checkpoint = args[0];
		return true;
	}
	if (name == "particlefx") {
		if (args.size() >= 2 && spawnParticles) {
			hgr::Node* at = node(args[1]);
			if (at) spawnParticles(args[0], at);
		}
		return true;
	}
	if (name == "playsound") {
		if (!args.empty() && playSound) playSound(args[0], args.size() > 1 && atoi(args[1].c_str()) != 0);
		return true;
	}
	if (name == "stopsound") {
		if (!args.empty() && stopSound) stopSound(args[0]);
		return true;
	}
	if (name == "addscore") {
		const float add = args.empty() ? 1.f : (float)atof(args[0].c_str());
		setVariable("score", getVariable("score") + add);
		printf("score: %d\n", (int)getVariable("score"));
		return true;
	}
	if (name == "setgamemode") {
		m_gameMode = args.empty() ? 0 : atoi(args[0].c_str());
		setVariable("gamemode", (float)m_gameMode);
		return true;
	}
	if (name == "levelcomplete" || name == "levelend") {
		if (!m_levelComplete) printf("*** level complete: %d points ***\n", (int)getVariable("score"));
		m_levelComplete = true;
		return true;
	}
	if (name == "setguidingobject" && !args.empty()) {
		BallRequest r;
		r.kind = BallRequest::SET_GUIDE;
		r.name = args[0];
		m_ballRequests.push_back(r);
		return true;
	}
	if (name == "attachtorail" && args.size() >= 2) {
		BallRequest r;
		r.kind = args[0] == "0" ? BallRequest::DETACH_RAIL : BallRequest::ATTACH_RAIL;
		r.name = args[1];
		m_ballRequests.push_back(r);
		return true;
	}
	if (name == "speedmul" && args.size() >= 2) {
		BallRequest r;
		r.kind = BallRequest::SPEED_MUL;
		r.value = resolve(args[1]);
		m_ballRequests.push_back(r);
		return true;
	}
	if (name == "maxspeed" && !args.empty()) {
		BallRequest r;
		r.kind = BallRequest::MAX_SPEED;
		r.value = resolve(args[0]);
		m_ballRequests.push_back(r);
		return true;
	}
	if (name == "setobjectpos" && args.size() >= 2 && args[0] == "G_ballforms") {
		BallRequest r;
		r.kind = BallRequest::SET_POS;
		r.name = args[1];
		m_ballRequests.push_back(r);
		return true;
	}
	if (name == "setcameraview" && args.size() >= 2) {
		m_camViewA = resolve(args[0]);
		m_camViewB = resolve(args[1]);
		m_camViewSet = true;
		return true;
	}
	if (name == "pressure") { if (onPressure) onPressure(args.empty() ? 1.f : resolve(args[0])); return true; }
	if (name == "setballdirection" || name == "setcamprops") return true;
	if (name == "updateballvalues") { m_updateBall = true; return true; }
	if (name == "resetballvalues") { m_resetBall = true; return true; }
	if (name == "levelfailed") {
		m_respawn = true;
		return true;
	}
	if (m_unhandled.insert(name).second) {
		std::string a;
		for (const std::string& x : args) a += " " + x;
		printf("[world] action '%s' not implemented yet (e.g.:%s)\n", name.c_str(), a.c_str());
	}
	return true;
}

std::vector<std::string> SceneWorld::runningTriggers() const {
	std::vector<std::string> r;
	for (const auto& t : m_triggers)
		if (t.second->state() == Trigger::RUNNING || t.second->state() == Trigger::EXITING) r.push_back(t.first);
	return r;
}

void SceneWorld::update(float dt) {
	setVariable("_dt", dt);
	for (auto& t : m_triggers) { m_currentTrigger = t.first; t.second->update(*this, dt); }
	m_currentTrigger.clear();

	math::float3x4 tm;
	for (std::map<int, Playing>::iterator it = m_playing.begin(); it != m_playing.end();) {
		Playing& p = it->second;
		if (!p.done) {
			p.time += dt;
			if (p.time >= p.end) {
				if (p.loop) p.time = p.start + (p.end > p.start ? fmodf(p.time - p.start, p.end - p.start) : 0.f);
				else { p.time = p.end; p.done = true; }
			}
			p.anim->eval(p.time, &tm);
			p.node->setTransform(tm);
		}
		if (p.done && p.time > p.end + 10.f) it = m_playing.erase(it);
		else { if (p.done) p.time += dt; ++it; }
	}

	for (std::map<int, Moving>::iterator it = m_moving.begin(); it != m_moving.end();) {
		Moving& m = it->second;
		if (!m.done) {
			float step = dt;
			if (m.kind != UV) {
				if (m.elapsed + step >= m.duration) { step = m.duration - m.elapsed; m.done = true; }
				m.elapsed += step;
			}
			switch (m.kind) {
			case MOVE:
				m.node->setPosition(m.node->position() + m.rate * step);
				break;
			case ROTATE: {
				const float deg = sqrtf(m.rate.x * m.rate.x + m.rate.y * m.rate.y + m.rate.z * m.rate.z);
				if (deg > 0.f) {
					const math::float3 axis = m.rate * (1.f / deg);
					m.node->setRotation(m.node->rotation() * math::float3x3(axis, deg * step * 3.14159265f / 180.f));
				}
				break;
			}
			case SCALE: {
				math::float3x4 tm = m.node->transform();
				const math::float3 k(1.f + m.rate.x * step, 1.f + m.rate.y * step, 1.f + m.rate.z * step);
				for (int r = 0; r < 3; ++r) { tm(r, 0) *= k.x; tm(r, 1) *= k.y; tm(r, 2) *= k.z; }
				m.node->setTransform(tm);
				break;
			}
			case UV:
				m.u = fmodf(m.u + m.rate.x * step, 1.f);
				m.v = fmodf(m.v + m.rate.y * step, 1.f);
				setUvOffset(m.node, m.u, m.v);
				m_uvOffset[m.node] = std::make_pair(m.u, m.v);
				break;
			}
		}
		if (m.done && (m.elapsed += dt) > m.duration + 10.f) it = m_moving.erase(it);
		else ++it;
	}
}

}
