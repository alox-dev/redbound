#include "SceneTarget.h"
#include <GLES2/gl2.h>
#include <cstdio>

namespace redbound {

static GLuint compile(GLenum type, const char* source) {
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, 0);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

SceneTarget::~SceneTarget() {
	release();
	if (m_program) glDeleteProgram(m_program);
	if (m_buffer) glDeleteBuffers(1, &m_buffer);
}

void SceneTarget::release() {
	if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
	if (m_texture) glDeleteTextures(1, &m_texture);
	if (m_depth) glDeleteRenderbuffers(1, &m_depth);
	m_fbo = m_texture = m_depth = 0;
	m_width = m_height = 0;
}

bool SceneTarget::begin(int width, int height) {
	if (m_failed || width <= 0 || height <= 0) return false;
	if (width != m_width || height != m_height) {
		release();
		glGenTextures(1, &m_texture);
		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		glGenRenderbuffers(1, &m_depth);
		glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
		glGenFramebuffers(1, &m_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			printf("render scale: framebuffer not supported, rendering at full resolution\n");
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			release();
			m_failed = true;
			return false;
		}
		m_width = width;
		m_height = height;
	}
	if (!m_program) {
		const GLuint vs = compile(GL_VERTEX_SHADER, "attribute vec2 p; varying vec2 v; void main() { v = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }");
		const GLuint fs = compile(GL_FRAGMENT_SHADER, "precision mediump float; uniform sampler2D t; varying vec2 v; void main() { gl_FragColor = texture2D(t, v); }");
		if (!vs || !fs) {
			m_failed = true;
			return false;
		}
		m_program = glCreateProgram();
		glAttachShader(m_program, vs);
		glAttachShader(m_program, fs);
		glLinkProgram(m_program);
		glDeleteShader(vs);
		glDeleteShader(fs);
		m_position = glGetAttribLocation(m_program, "p");
		m_sampler = glGetUniformLocation(m_program, "t");
		const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
		glGenBuffers(1, &m_buffer);
		glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
	glViewport(0, 0, width, height);
	return true;
}

void SceneTarget::end(int screenWidth, int screenHeight) {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, screenWidth, screenHeight);
	GLint program = 0, buffer = 0, texture = 0, active = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
	GLboolean mask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glUseProgram(m_program);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glUniform1i(m_sampler, 0);
	glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
	glEnableVertexAttribArray(m_position);
	glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(m_position);
	glUseProgram(program);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(active);
	if (depth) glEnable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND);
	if (cull) glEnable(GL_CULL_FACE);
	glDepthMask(mask);
}

}
