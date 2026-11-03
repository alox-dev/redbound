#include "AmbientSounds.h"
#include "Audio.h"
#include "FileUtils.h"
#include <hgr/Scene.h>
#include <hgr/UserPropertySet.h>
#include <lang/Hashtable.h>
#include <cstdio>
#include <cstdlib>

namespace redbound {

void AmbientSounds::load(hgr::Scene* scene, Audio& audio) {
	m_sounds.clear();
	hgr::UserPropertySet* props = audio.ok() ? scene->userProperties() : 0;
	if (!props) return;
	for (lang::HashtableIterator<lang::String, lang::String> it = props->begin(); it != props->end(); ++it) {
		const std::string key = it.key().c_str(), text = it.value().c_str();
		const size_t music = text.find("music=");
		if (music != std::string::npos && (music == 0 || text[music - 1] == '\n')) {
			const size_t end = text.find('\n', music);
			std::string file = text.substr(music + 6, end == std::string::npos ? std::string::npos : end - music - 6);
			while (!file.empty() && (file.back() == '\r' || file.back() == ' ')) file.pop_back();
			if (!file.empty()) {
				audio.playMusic(file);
				printf("audio: music %s\n", file.c_str());
			}
		}
		if (key.compare(0, 2, "S_") != 0) continue;
		std::string file;
		float inner = 300.f, outer = 1500.f, volume = 1.f;
		size_t a = 0;
		while (a < text.size()) {
			const size_t e = text.find('\n', a);
			std::string line = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
			a = e == std::string::npos ? text.size() : e + 1;
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
			const size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			const std::string k = line.substr(0, eq), v = line.substr(eq + 1), lv = toLower(v);
			if (endsWith(lv, ".wav") || endsWith(lv, ".mp3")) file = v;
			else if (k == "inradius") inner = (float)atof(v.c_str());
			else if (k == "outradius") outer = (float)atof(v.c_str());
			else if (k == "volume") volume = (float)atof(v.c_str());
		}
		hgr::Node* node = scene->findNodeByName(key.c_str());
		if (file.empty() || !node) continue;
		Sound s;
		s.node = node;
		s.inner = inner;
		s.outer = outer > inner ? outer : inner + 1.f;
		s.volume = volume > 1.5f ? volume / 100.f : volume;
		s.voice = audio.play(file, true, 0.f);
		m_sounds.push_back(s);
	}
	printf("audio: %d ambient sounds\n", (int)m_sounds.size());
}

void AmbientSounds::update(hgr::Scene* scene, Audio& audio, const math::float3& listener) {
	for (Sound& s : m_sounds) {
		bool linked = false;
		for (hgr::Node* p = s.node; p; p = p->parent())
			if (p == (hgr::Node*)scene) { linked = true; break; }
		const float dist = (s.node->worldTransform().translation() - listener).length();
		float gain = dist <= s.inner ? 1.f : dist >= s.outer ? 0.f : 1.f - (dist - s.inner) / (s.outer - s.inner);
		if (!linked) gain = 0.f;
		audio.setVoiceVolume(s.voice, gain * s.volume);
	}
}

}
