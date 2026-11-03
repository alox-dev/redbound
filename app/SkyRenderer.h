#pragma once
#include <GLES2/gl2.h>
#include <vector>

namespace hgr { class Node; class Camera; }

namespace redbound {

class SkyRenderer {
public:
	bool init(hgr::Node* sky);
	void draw(hgr::Camera* camera, int width, int height);
private:
	std::vector<float> m_verts;
	std::vector<unsigned short> m_idx;
	GLuint m_tex = 0, m_prog = 0;
	GLenum m_mode = GL_TRIANGLES;
};

}
