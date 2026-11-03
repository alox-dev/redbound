#pragma once
#include <GLES2/gl2.h>

namespace redbound {

class FadeOverlay {
public:
	void draw(float amount, bool white);
private:
	GLuint m_program = 0;
	GLint m_color = -1;
	void init();
};

}
