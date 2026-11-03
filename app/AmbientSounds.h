#pragma once
#include <lang/Object.h>
#include <math/float3.h>
#include <string>
#include <vector>

namespace hgr { class Scene; class Node; }

namespace redbound {

class Audio;

class AmbientSounds {
public:
	void load(hgr::Scene* scene, Audio& audio);
	void update(hgr::Scene* scene, Audio& audio, const math::float3& listener);
	size_t count() const { return m_sounds.size(); }
private:
	struct Sound {
		P(hgr::Node) node;
		int voice;
		float inner, outer, volume;
	};
	std::vector<Sound> m_sounds;
};

}
