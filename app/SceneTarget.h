#pragma once

namespace redbound {

class SceneTarget {
public:
	~SceneTarget();
	bool begin(int width, int height);
	void end(int screenWidth, int screenHeight);
	unsigned framebuffer() const { return m_fbo; }

private:
	void release();
	unsigned m_fbo = 0, m_texture = 0, m_depth = 0, m_program = 0, m_buffer = 0;
	int m_width = 0, m_height = 0, m_position = -1, m_sampler = -1;
	bool m_failed = false;
};

}
