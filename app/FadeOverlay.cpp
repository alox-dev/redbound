#include "FadeOverlay.h"

namespace redbound {

void FadeOverlay::draw(float amount, bool white) {
	if (amount <= 0.001f) return;
	if (!m_program) init();
	const float c = white ? 1.f : 0.f;
	glUseProgram(m_program);
	glUniform4f(m_color, c, c, c, amount > 1.f ? 1.f : amount);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void FadeOverlay::init() {
	const char* vs = "attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }";
	const char* fs = "precision mediump float; uniform vec4 c; void main() { gl_FragColor = c; }";
	GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, 0);
	glCompileShader(v);
	glShaderSource(f, 1, &fs, 0);
	glCompileShader(f);
	m_program = glCreateProgram();
	glAttachShader(m_program, v);
	glAttachShader(m_program, f);
	glBindAttribLocation(m_program, 0, "p");
	glLinkProgram(m_program);
	glDeleteShader(v);
	glDeleteShader(f);
	m_color = glGetUniformLocation(m_program, "c");
}

}
