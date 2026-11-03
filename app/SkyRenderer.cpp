#include "SkyRenderer.h"
#include "GLES2.h"
#include <gr/Primitive.h>
#include <gr/Shader.h>
#include <hgr/Camera.h>
#include <hgr/Mesh.h>
#include <cmath>
#include <cstdio>

namespace redbound {

bool SkyRenderer::init(hgr::Node* sky) {
	hgr::Mesh* mesh = 0;
	for (hgr::Node* c = sky; c && !mesh; c = c->next(sky)) if (c->classId() == hgr::Node::NODE_MESH) mesh = static_cast<hgr::Mesh*>(c);
	if (!mesh || mesh->primitives() < 1) return false;
	gr::Primitive* prim = mesh->getPrimitive(0);
	const int nv = prim->vertices(), ni = prim->indices();
	std::vector<math::float4> pos(nv), uv(nv);
	std::vector<int> idx(ni > 0 ? ni : 0);
	{
		gr::Primitive::Lock lk(prim, gr::Primitive::LOCK_READ);
		prim->getVertexPositions(0, &pos[0], nv);
		prim->getVertexTextureCoordinates(0, 0, &uv[0], nv);
		if (ni > 0) prim->getIndices(0, &idx[0], ni);
	}
	math::float3x4 tm = mesh->worldTransform();
	math::float3 c(0, 0, 0);
	std::vector<math::float3> w(nv);
	for (int i = 0; i < nv; ++i) { w[i] = tm.transform(math::float3(pos[i].x, pos[i].y, pos[i].z)); c += w[i]; }
	c *= 1.f / (nv > 0 ? nv : 1);
	m_verts.clear();
	for (int i = 0; i < nv; ++i) {
		const math::float3 d = w[i] - c;
		m_verts.push_back(d.x); m_verts.push_back(d.y); m_verts.push_back(d.z);
		m_verts.push_back(uv[i].x); m_verts.push_back(uv[i].y);
	}
	m_idx.clear();
	if (ni > 0) for (int i = 0; i < ni; ++i) m_idx.push_back((unsigned short)idx[i]);
	else for (int i = 0; i < nv; ++i) m_idx.push_back((unsigned short)i);
	m_mode = prim->type() == gr::Primitive::PRIM_TRISTRIP ? GL_TRIANGLE_STRIP : prim->type() == gr::Primitive::PRIM_TRIFAN ? GL_TRIANGLE_FAN : GL_TRIANGLES;
	gr::Shader* fx = prim->shader();
	gr::BaseTexture* t = fx ? fx->getTexture("BASEMAP") : 0;
	gr::GLES2_Texture* gt = dynamic_cast<gr::GLES2_Texture*>(t);
	m_tex = gt ? gt->id() : 0;
	printf("sky: rendered separately, %d vertices, %d indices, texture %s\n", nv, (int)m_idx.size(), m_tex ? "BASEMAP" : "(not found)");
	return m_tex != 0;
}

void SkyRenderer::draw(hgr::Camera* cam, int width, int height) {
	if (!m_tex || m_idx.empty()) return;
	if (!m_prog) {
		const char* vs = "attribute vec3 p; attribute vec2 t; varying vec2 v; uniform mat3 rot; uniform vec2 proj;\n"
		                 "void main() { v = t; vec3 q = rot * p; gl_Position = vec4(q.x * proj.x, q.y * proj.y, q.z * 0.9999, q.z); }";
		const char* fs = "precision mediump float; varying vec2 v; uniform sampler2D tex; void main() { gl_FragColor = vec4(texture2D(tex, v).rgb, 1.0); }";
		GLuint a = glCreateShader(GL_VERTEX_SHADER), b = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(a, 1, &vs, 0); glCompileShader(a);
		glShaderSource(b, 1, &fs, 0); glCompileShader(b);
		m_prog = glCreateProgram();
		glAttachShader(m_prog, a); glAttachShader(m_prog, b);
		glBindAttribLocation(m_prog, 0, "p"); glBindAttribLocation(m_prog, 1, "t");
		glLinkProgram(m_prog);
		glDeleteShader(a); glDeleteShader(b);
	}
	const math::float3x4 view = cam->viewTransform();
	float m[9];
	for (int col = 0; col < 3; ++col) for (int row = 0; row < 3; ++row) m[col * 3 + row] = view(row, col);
	const float sx = 1.f / tanf(cam->horizontalFov() * 0.5f);
	const float sy = sx * (float)width / (float)(height > 0 ? height : 1);
	glUseProgram(m_prog);
	glUniformMatrix3fv(glGetUniformLocation(m_prog, "rot"), 1, GL_FALSE, m);
	glUniform2f(glGetUniformLocation(m_prog, "proj"), sx, sy);
	glUniform1i(glGetUniformLocation(m_prog, "tex"), 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_tex);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), &m_verts[0]);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), &m_verts[3]);
	glDrawElements(m_mode, (GLsizei)m_idx.size(), GL_UNSIGNED_SHORT, &m_idx[0]);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

}
