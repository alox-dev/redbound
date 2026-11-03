#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "dr_wav.h"
#include "dr_mp3.h"
#include "Audio.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <cmath>

namespace redbound {

static const int RATE = 44100;

static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

static void indexDir(const std::string& dir, std::map<std::string, std::string>& out, int depth) {
	if (depth > 6) return;
	DIR* d = opendir(dir.c_str());
	if (!d) return;
	while (struct dirent* e = readdir(d)) {
		const std::string n = e->d_name;
		if (n == "." || n == "..") continue;
		const std::string p = dir + "/" + n;
		struct stat st;
		if (stat(p.c_str(), &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) { indexDir(p, out, depth + 1); continue; }
		const std::string l = lower(n);
		if (l.size() > 4 && (l.compare(l.size() - 4, 4, ".wav") == 0 || l.compare(l.size() - 4, 4, ".mp3") == 0))
			if (!out.count(l)) out[l] = p;
	}
	closedir(d);
}

Audio::Audio() {}

Audio::~Audio() {
	if (m_dev) SDL_CloseAudioDevice(m_dev);
}

bool Audio::init(const std::string& dataRoot) {
	indexDir(dataRoot, m_index, 0);
	if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "audio: could not initialize SDL audio: %s\n", SDL_GetError());
		return false;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = &Audio::callback;
	want.userdata = this;
	m_dev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
	if (!m_dev) {
		fprintf(stderr, "audio: no device (%s); the game continues without sound\n", SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(m_dev, 0);
	printf("audio: %d sound and music files indexed\n", (int)m_index.size());
	return true;
}

std::shared_ptr<Audio::Clip> Audio::load(const std::string& name) {
	const std::string key = lower(name.substr(name.find_last_of("/\\") == std::string::npos ? 0 : name.find_last_of("/\\") + 1));
	auto c = m_cache.find(key);
	if (c != m_cache.end()) return c->second;
	auto it = m_index.find(key);
	if (it == m_index.end()) {
		fprintf(stderr, "audio: '%s' not found\n", name.c_str());
		m_cache[key] = nullptr;
		return nullptr;
	}
	unsigned int channels = 0, rate = 0;
	drwav_uint64 frames = 0;
	int16_t* pcm = 0;
	bool mp3 = key.size() > 4 && key.compare(key.size() - 4, 4, ".mp3") == 0;
	if (mp3) {
		drmp3_config cfg;
		drmp3_uint64 f = 0;
		pcm = drmp3_open_file_and_read_pcm_frames_s16(it->second.c_str(), &cfg, &f, 0);
		channels = cfg.channels;
		rate = cfg.sampleRate;
		frames = f;
	} else {
		pcm = drwav_open_file_and_read_pcm_frames_s16(it->second.c_str(), &channels, &rate, &frames, 0);
	}
	if (!pcm || !channels || !rate) {
		fprintf(stderr, "audio: could not decode '%s'\n", it->second.c_str());
		m_cache[key] = nullptr;
		return nullptr;
	}
	auto clip = std::make_shared<Clip>();
	const double step = (double)rate / RATE;
	const size_t outFrames = (size_t)((double)frames / step);
	clip->samples.resize(outFrames * 2);
	for (size_t i = 0; i < outFrames; ++i) {
		const double src = i * step;
		const size_t a = (size_t)src;
		const size_t b = std::min((size_t)frames - 1, a + 1);
		const float t = (float)(src - a);
		for (int ch = 0; ch < 2; ++ch) {
			const unsigned sc = channels == 1 ? 0 : (unsigned)ch;
			const float va = pcm[a * channels + sc], vb = pcm[b * channels + sc];
			clip->samples[i * 2 + ch] = (int16_t)(va + (vb - va) * t);
		}
	}
	if (mp3) drmp3_free(pcm, 0); else drwav_free(pcm, 0);
	m_cache[key] = clip;
	return clip;
}

int Audio::play(const std::string& name, bool loop, float volume) {
	if (!m_dev) return -1;
	std::shared_ptr<Clip> clip = load(name);
	if (!clip || clip->samples.empty()) return -1;
	std::lock_guard<std::mutex> lk(m_mutex);
	Voice v;
	v.clip = clip;
	v.name = lower(name);
	v.pos = 0;
	v.loop = loop;
	v.volume = volume;
	v.id = m_nextId++;
	v.music = false;
	m_voices.push_back(v);
	return v.id;
}

void Audio::stop(const std::string& name) {
	const std::string n = lower(name);
	std::lock_guard<std::mutex> lk(m_mutex);
	m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&](const Voice& v) { return !v.music && v.name == n; }), m_voices.end());
}

void Audio::stopVoice(int id) {
	std::lock_guard<std::mutex> lk(m_mutex);
	m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&](const Voice& v) { return v.id == id; }), m_voices.end());
}

bool Audio::isPlaying(int id) const {
	std::lock_guard<std::mutex> lk(m_mutex);
	for (const Voice& v : m_voices) if (v.id == id) return true;
	return false;
}

void Audio::setVoiceVolume(int id, float volume) {
	std::lock_guard<std::mutex> lk(m_mutex);
	for (Voice& v : m_voices) if (v.id == id) v.volume = volume;
}

void Audio::playMusic(const std::string& name, float volume) {
	if (!m_dev) return;
	std::shared_ptr<Clip> clip = load(name);
	if (!clip || clip->samples.empty()) return;
	std::lock_guard<std::mutex> lk(m_mutex);
	m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice& v) { return v.music; }), m_voices.end());
	Voice v;
	v.clip = clip;
	v.name = lower(name);
	v.pos = 0;
	v.loop = true;
	v.volume = volume;
	v.id = m_nextId++;
	v.music = true;
	m_voices.push_back(v);
}

void Audio::stopMusic() {
	std::lock_guard<std::mutex> lk(m_mutex);
	m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice& v) { return v.music; }), m_voices.end());
}

void Audio::callback(void* user, Uint8* stream, int len) {
	static_cast<Audio*>(user)->mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

void Audio::mix(int16_t* out, int frames) {
	std::vector<float> acc((size_t)frames * 2, 0.f);
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		for (Voice& v : m_voices) {
			const std::vector<int16_t>& s = v.clip->samples;
			const size_t total = s.size() / 2;
			for (int i = 0; i < frames; ++i) {
				if (v.pos >= total) {
					if (!v.loop) break;
					v.pos = 0;
				}
				acc[i * 2] += s[v.pos * 2] * v.volume;
				acc[i * 2 + 1] += s[v.pos * 2 + 1] * v.volume;
				++v.pos;
			}
		}
		m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
		                              [](const Voice& v) { return !v.loop && v.pos >= v.clip->samples.size() / 2; }),
		               m_voices.end());
	}
	const float master = m_muted ? 0.f : m_master;
	for (int i = 0; i < frames * 2; ++i) {
		float x = acc[i] * master;
		if (x > 32767.f || x < -32768.f) x = 32767.f * std::tanh(x / 32767.f);
		out[i] = (int16_t)std::max(-32768.f, std::min(32767.f, x));
	}
}

}
