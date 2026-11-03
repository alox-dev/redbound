#include "NullContext.h"
#include <hgr/Scene.h>
#include <hgr/Node.h>
#include <hgr/UserPropertySet.h>
#include <hgr/Mesh.h>
#include <gr/impl/DIPrimitive.h>
#include <gr/VertexFormat.h>
#include "ImageLoader.h"
#include <float.h>
#include <hgr/SceneInputStream.h>
#include <io/FileInputStream.h>
#include <lang/Throwable.h>
#include <lang/Format.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>

static const char* className(int id) {
	switch (id & 0xF0) {
	case hgr::Node::NODE_CAMERA: return "Camera";
	case hgr::Node::NODE_CONSOLE: return "Console";
	case hgr::Node::NODE_DUMMY: return "Dummy";
	case hgr::Node::NODE_LIGHT: return "Light";
	case hgr::Node::NODE_SCENE: return "Scene";
	case hgr::Node::NODE_LINES: return "Lines";
	case hgr::Node::NODE_MESH: return "Mesh";
	case hgr::Node::NODE_OTHER_VISUAL: return "Visual";
	case hgr::Node::NODE_PARTICLESYSTEM: return "ParticleSystem";
	default: return "Node";
	}
}

static int depthOf(const hgr::Node* n, const hgr::Node* root) {
	int d = 0;
	for (const hgr::Node* p = n->parent(); p && p != root; p = p->parent()) ++d;
	return d;
}

static bool isDir(const std::string& p) {
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string withSlash(std::string p) {
	if (!p.empty() && p[p.size() - 1] != '/') p += '/';
	return p;
}

static std::string parentDir(const std::string& f) {
	size_t i = f.find_last_of('/');
	return i == std::string::npos ? std::string(".") : f.substr(0, i);
}

int main(int argc, char** argv) {
	std::string data, particles, textures, shaders;
	bool prims = false, lmcheck = false;
	const char* file = 0;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--data" && i + 1 < argc) data = argv[++i];
		else if (a == "--particles" && i + 1 < argc) particles = argv[++i];
		else if (a == "--textures" && i + 1 < argc) textures = argv[++i];
		else if (a == "--shaders" && i + 1 < argc) shaders = argv[++i];
		else if (a == "--prims") prims = true;
		else if (a == "--lmcheck") lmcheck = true;
		else file = argv[i];
	}
	if (!file) { fprintf(stderr, "usage: %s [--data DIR] [--particles DIR] [--textures DIR] [--shaders DIR] <file.hgr>\n", argv[0]); return 1; }

	if (data.empty()) {
		std::string up2 = parentDir(parentDir(parentDir(file)));
		std::string dir = parentDir(file);
		if (dir == ".") up2 = "../..";
		if (isDir(up2 + "/resources/particles")) data = up2;
	}
	if (!data.empty()) {
		if (particles.empty()) particles = data + "/resources/particles";
		if (shaders.empty() && isDir(data + "/shaders")) shaders = data + "/shaders";
	}
	std::string lmTextures = textures;
	if (lmTextures.empty() && !data.empty()) lmTextures = data + "/resources/textures";
	if (!lmTextures.empty()) lmTextures = withSlash(lmTextures);
	if (!particles.empty()) particles = withSlash(particles);
	if (!textures.empty()) textures = withSlash(textures);
	if (!shaders.empty()) shaders = withSlash(shaders);
	try {
		gr::Context::PlatformType platform;
		{
			io::FileInputStream fin(file);
			hgr::SceneInputStream hdr(&fin);
			platform = hdr.platform();
			printf("== Header\nformat version: %d.%02d\nexporter version: 0x%06x\nplatform: %d (%s)\n\n",
				hdr.version() / 100, hdr.version() % 100, hdr.exporterVersion(), (int)platform, gr::Context::getString(platform));
			printf("paths: data='%s' particles='%s' shaders='%s'\n\n", data.c_str(), particles.c_str(), shaders.c_str());
		}
		P(NullContext) ctx = new NullContext(platform);
		P(hgr::Scene) scene = new hgr::Scene(ctx, file, 0, textures.c_str(), shaders.c_str(), particles.c_str());

		printf("== Nodos\n");
		int count = 0;
		for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
			printf("%*s%s [%s]\n", depthOf(n, scene) * 2, "", n->name().c_str(), className(n->classId()));
			++count;
		}
		printf("(%d nodos)\n", count);

		if (prims) {
			printf("\n== Primitivas\n");
			for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
				if (n->classId() != hgr::Node::NODE_MESH) continue;
				hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
				for (int p = 0; p < mesh->primitives(); ++p) {
					gr::DIPrimitive* prim = dynamic_cast<gr::DIPrimitive*>(mesh->getPrimitive(p));
					if (!prim) continue;
					const math::float4 psb = prim->vertexPositionScaleBias(), tsb = prim->vertexTextureCoordinateScaleBias();
					printf("%s[%d] material='%s' verts=%d vf=%s\n  pos sb=(%g %g %g %g) uv sb=(%g %g %g %g)\n",
					       n->name().c_str(), p, prim->shader() ? prim->shader()->name().c_str() : "?", prim->vertices(),
					       prim->vertexFormat().toString().c_str(), psb.x, psb.y, psb.z, psb.w, tsb.x, tsb.y, tsb.z, tsb.w);
					gr::ContextObject::Lock lk(prim, gr::ContextObject::LOCK_READ);
					for (int layer = 0; layer < 4; ++layer) {
						if (!prim->vertexFormat().hasData((gr::VertexFormat::DataType)(gr::VertexFormat::DT_TEX0 + layer))) continue;
						std::vector<math::float4> uv(prim->vertices());
						prim->getVertexTextureCoordinates(0, layer, &uv[0], (int)uv.size());
						float mn[2] = { FLT_MAX, FLT_MAX }, mx[2] = { -FLT_MAX, -FLT_MAX };
						for (size_t k = 0; k < uv.size(); ++k) {
							mn[0] = std::min(mn[0], uv[k].x); mx[0] = std::max(mx[0], uv[k].x);
							mn[1] = std::min(mn[1], uv[k].y); mx[1] = std::max(mx[1], uv[k].y);
						}
						printf("  TEX%d %s crudo u[%g..%g] v[%g..%g]\n", layer,
						       gr::VertexFormat::toString(prim->vertexFormat().getDataFormat((gr::VertexFormat::DataType)(gr::VertexFormat::DT_TEX0 + layer))),
						       mn[0], mx[0], mn[1], mx[1]);
					}
				}
			}
		}

		if (lmcheck) {
			static const char* const NAMES[] = { "identidad", "voltear V", "voltear U", "voltear U y V", "intercambiar UV", "intercambiar + voltear V", "intercambiar + voltear U", "intercambiar + voltear ambas" };
			const int T = 8;
			long total = 0, black[8] = { 0 };
			std::map<std::string, redbound::Image> cache;
			std::map<std::string, std::vector<long> > perMat;
			std::map<std::string, long> perMatTotal;
			int cPrims = 0, cShader = 0, cSimap = 0, cTex1 = 0, cTexObj = 0, cImg = 0;
			std::map<std::string, int> paramNames;
			std::vector<std::string> lmErrors;
			double lumSum = 0;
			std::map<std::string, double> lumMat;
			for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
				if (n->classId() != hgr::Node::NODE_MESH) continue;
				hgr::Mesh* mesh = static_cast<hgr::Mesh*>(n);
				for (int p = 0; p < mesh->primitives(); ++p) {
					++cPrims;
					gr::DIPrimitive* prim = dynamic_cast<gr::DIPrimitive*>(mesh->getPrimitive(p));
					NullShader* fx = prim ? dynamic_cast<NullShader*>(prim->shader()) : 0;
					if (!fx) continue;
					++cShader;
					for (std::map<std::string, P(gr::BaseTexture)>::iterator ti = fx->textures.begin(); ti != fx->textures.end(); ++ti) ++paramNames[ti->first];
					if (!fx->textures.count("SIMAP")) continue;
					++cSimap;
					if (!prim->vertexFormat().hasData(gr::VertexFormat::DT_TEX1)) continue;
					++cTex1;
					NullTexture* lt = dynamic_cast<NullTexture*>((gr::BaseTexture*)fx->textures["SIMAP"]);
					if (!lt) continue;
					++cTexObj;
					std::string f = lt->filename;
					size_t sl = f.find_last_of("/\\");
					std::string base = sl == std::string::npos ? f : f.substr(sl + 1);
					size_t dot = base.find_last_of('.');
					std::string path = lmTextures + (dot == std::string::npos ? base : base.substr(0, dot)) + ".pvr";
					if (!cache.count(path)) {
						redbound::Image im;
						if (!redbound::loadImage(path, im)) lmErrors.push_back(path + ": " + im.error);
						cache[path] = im;
					}
					const redbound::Image& im = cache[path];
					if (im.levels.empty()) continue;
					++cImg;
					const redbound::ImageLevel& lv = im.levels[0];
					gr::ContextObject::Lock lk(prim, gr::ContextObject::LOCK_READ);
					std::vector<math::float4> uv(prim->vertices());
					prim->getVertexTextureCoordinates(0, 1, &uv[0], (int)uv.size());
					std::string mat = std::string(fx->name().c_str()) + " / " + base;
					if (!perMat.count(mat)) perMat[mat].assign(T, 0);
					for (size_t k = 0; k < uv.size(); ++k) {
						float u = uv[k].x, v = uv[k].y;
						if (u < -0.01f || u > 1.01f || v < -0.01f || v > 1.01f) continue;
						++total; ++perMatTotal[mat];
						for (int t = 0; t < T; ++t) {
							float a = u, b = v;
							if (t >= 4) std::swap(a, b);
							int m = t % 4;
							if (m == 1 || m == 3) b = 1.f - b;
							if (m == 2 || m == 3) a = 1.f - a;
							int x = std::min(lv.width - 1, std::max(0, (int)(a * lv.width)));
							int y = std::min(lv.height - 1, std::max(0, (int)(b * lv.height)));
							const unsigned char* px = &lv.rgba[((size_t)y * lv.width + x) * 4];
							if (px[0] + px[1] + px[2] < 24) { ++black[t]; ++perMat[mat][t]; }
							if (t == 0) { const double l = (px[0] + px[1] + px[2]) / (3.0 * 255.0); lumSum += l; lumMat[mat] += l; }
						}
					}
				}
			}
			printf("\n== Lightmap orientation test (%ld vertices with TEX1 in 0..1)\n", total);
			printf("  primitives: %d | with shader: %d | with SIMAP: %d | with TEX1: %d | valid texture: %d | atlas loaded: %d\n",
			       cPrims, cShader, cSimap, cTex1, cTexObj, cImg);
			printf("  material textures seen:");
			for (std::map<std::string, int>::iterator pi = paramNames.begin(); pi != paramNames.end(); ++pi) printf(" %s(%d)", pi->first.c_str(), pi->second);
			printf("\n  texture folder: '%s'\n", lmTextures.c_str());
			for (size_t e = 0; e < lmErrors.size() && e < 10; ++e) printf("  error: %s\n", lmErrors[e].c_str());
			for (int t = 0; t < T; ++t)
				printf("  %-28s %5.1f%% caen en fondo negro\n", NAMES[t], total ? 100.0 * black[t] / total : 0.0);
			printf("\n  Mean lightmap value at the vertices (identity orientation): %.3f  (0.5 = neutral in Rovio's shaders)\n",
			       total ? lumSum / total : 0.0);
			printf("\n  Per material (identity / flip V / flip U / flip both), %% black, and mean lightmap value:\n");
			for (std::map<std::string, std::vector<long> >::iterator it = perMat.begin(); it != perMat.end(); ++it) {
				long tt = perMatTotal[it->first];
				if (!tt) continue;
				printf("  %-55s %5.1f %5.1f %5.1f %5.1f  lm=%.2f  (%ld vert)\n", it->first.c_str(),
				       100.0 * it->second[0] / tt, 100.0 * it->second[1] / tt, 100.0 * it->second[2] / tt, 100.0 * it->second[3] / tt,
				       lumMat[it->first] / tt, tt);
			}
		}

		printf("\n== User properties\n");
		hgr::UserPropertySet* ups = scene->userProperties();
		if (ups) {
			for (lang::HashtableIterator<lang::String,lang::String> it = ups->begin(); it != ups->end(); ++it)
				printf("--- %s\n%s\n", it.key().c_str(), it.value().c_str());
		} else {
			printf("(none)\n");
		}
	} catch (lang::Throwable& e) {
		fprintf(stderr, "ERROR: %s\n", e.getMessage().format().c_str());
		return 2;
	}
	return 0;
}
