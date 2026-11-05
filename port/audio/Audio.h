#pragma once
#include <SDL.h>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

namespace redbound {

class Audio {
public:
	Audio();
	~Audio();

	bool init(const std::string& dataRoot);

	int play(const std::string& name, bool loop = false, float volume = 1.f);
	void stop(const std::string& name);
	void stopVoice(int id);
	bool isPlaying(int id) const;
	void setVoiceVolume(int id, float volume);

	void playMusic(const std::string& name, float volume = 0.7f);
	void stopMusic();

	void setMuted(bool m) { m_muted = m; }
	void setMasterVolume(float v) { m_master = clamp01(v); }
	void setMusicMuted(bool m) { m_musicMuted = m; }
	void setMusicVolume(float v) { m_musicVolume = clamp01(v); }
	void setSoundsMuted(bool m) { m_soundsMuted = m; }
	void setSoundsVolume(float v) { m_soundsVolume = clamp01(v); }
	void setPaused(bool p) { m_paused = p; }
	bool paused() const { return m_paused; }
	bool muted() const { return m_muted; }
	bool ok() const { return m_dev != 0; }

private:
	struct Clip { std::vector<int16_t> samples; };
	struct Voice { std::shared_ptr<Clip> clip; std::string name; size_t pos; bool loop; float volume; int id; bool music; };

	SDL_AudioDeviceID m_dev = 0;
	std::map<std::string, std::string> m_index;
	std::map<std::string, std::shared_ptr<Clip>> m_cache;
	std::vector<Voice> m_voices;
	mutable std::mutex m_mutex;
	int m_nextId = 1;
	static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
	std::atomic<bool> m_muted{ false }, m_musicMuted{ false }, m_soundsMuted{ false }, m_paused{ false };
	std::atomic<float> m_master{ 1.f }, m_musicVolume{ 1.f }, m_soundsVolume{ 1.f };

	std::shared_ptr<Clip> load(const std::string& name);
	static void callback(void* user, Uint8* stream, int len);
	void mix(int16_t* out, int frames);
};

}
