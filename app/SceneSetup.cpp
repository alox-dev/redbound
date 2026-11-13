#include "SceneSetup.h"
#include "FileUtils.h"
#include "LaunchOptions.h"
#include <gr/Primitive.h>
#include <gr/Shader.h>
#include <hgr/Camera.h>
#include <hgr/Mesh.h>
#include <hgr/ResourceManager.h>
#include <hgr/Scene.h>
#include <hgr/UserPropertySet.h>
#include <lang/Format.h>
#include <lang/Hashtable.h>
#include <lang/Throwable.h>
#include <cstdio>
#include <cstdlib>

namespace redbound {

static bool traceEnabled() { return getenv("REDBOUND_TRACE") != 0; }

static bool isCollisionOnly(hgr::UserPropertySet* props, hgr::Node* node) {
	if (!props || !props->containsKey(node->name())) return false;
	if (node->name().startsWith("T_")) return false;
	const std::string text = props->get(node->name()).c_str();
	size_t at = text.find("group=");
	while (at != std::string::npos && at > 0 && text[at - 1] != '\n') at = text.find("group=", at + 1);
	if (at == std::string::npos) return false;
	const long group = strtol(text.c_str() + at + 6, 0, 10);
	return (group & 0x100) != 0;
}

static bool hasMaterial(hgr::Node* node) {
	if (node->classId() != hgr::Node::NODE_MESH) return true;
	hgr::Mesh* mesh = static_cast<hgr::Mesh*>(node);
	for (int p = 0; p < mesh->primitives(); ++p) {
		gr::Shader* fx = mesh->getPrimitive(p)->shader();
		if (fx && fx->name().length() > 0) return true;
	}
	return false;
}

std::vector<hgr::Node*> hideHelperNodes(hgr::Scene* scene) {
	std::vector<hgr::Node*> helpers;
	hgr::UserPropertySet* props = scene->userProperties();
	for (hgr::Node* n = scene->firstChild(); n; n = n->next(scene)) {
		if (!n->isVisualNode()) continue;
		if (!hasMaterial(n) || isCollisionOnly(props, n)) helpers.push_back(n);
	}
	for (hgr::Node* n : helpers) n->setEnabled(false);
	printf("hidden collision/trigger meshes: %d\n", (int)helpers.size());
	if (traceEnabled())
		for (hgr::Node* n : helpers) printf("[trace] hidden '%s'\n", n->name().c_str());
	return helpers;
}

static std::string lineValue(const std::string& text, const std::string& key) {
	size_t at = text.find(key + "=");
	while (at != std::string::npos && at > 0 && text[at - 1] != '\n') at = text.find(key + "=", at + 1);
	if (at == std::string::npos) return "";
	const size_t start = at + key.size() + 1;
	const size_t end = text.find('\n', start);
	std::string v = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
	while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
	return v;
}

std::string sceneProperty(hgr::Scene* scene, const std::string& node, const std::string& key) {
	hgr::UserPropertySet* props = scene->userProperties();
	if (!props || !props->containsKey(node.c_str())) return "";
	return lineValue(props->get(node.c_str()).c_str(), key);
}

static void disableShadows(hgr::Node* root) {
	for (hgr::Node* c = root; c; c = c->next(root))
		if (toLower(c->name().c_str()).find("shadow") != std::string::npos) c->setEnabled(false);
}

static void moveShadowsInto(hgr::Scene* model, hgr::Node* named) {
	std::vector<hgr::Node*> shadows;
	for (hgr::Node* c = model->firstChild(); c; c = c->next(model)) {
		if (c->classId() != hgr::Node::NODE_MESH || toLower(c->name().c_str()).find("shadow") == std::string::npos) continue;
		bool inside = false;
		for (hgr::Node* q = c; q; q = q->parent()) if (q == named) inside = true;
		if (!inside) shadows.push_back(c);
	}
	const math::float3x4 namedInverse = named->worldTransform().inverse();
	for (hgr::Node* s : shadows) {
		P(hgr::Node) keep = s;
		const math::float3x4 relative = namedInverse * s->worldTransform();
		keep->unlink();
		keep->setTransform(relative);
		keep->linkTo(named);
	}
}

int loadLibraryObjects(hgr::Scene* scene, gr::Context* ctx, hgr::ResourceManager* resources, const LaunchOptions& options,
                       std::vector<LibraryObject>& objects) {
	hgr::UserPropertySet* props = scene->userProperties();
	if (!props || options.dataDir.empty()) return 0;
	std::vector<std::pair<std::string, std::string>> requests;
	for (lang::HashtableIterator<lang::String, lang::String> it = props->begin(); it != props->end(); ++it) {
		const std::string load = lineValue(it.value().c_str(), "load");
		if (!load.empty()) requests.push_back(std::make_pair(std::string(it.key().c_str()), load));
	}
	const std::string modelDir = options.dataDir + "/resources/models/";
	int loaded = 0;
	for (const auto& request : requests) {
		hgr::Node* marker = scene->findNodeByName(request.first.c_str());
		if (!marker || !isDirectory(modelDir)) continue;
		std::string file = request.second, nodeName;
		const size_t comma = file.find(',');
		if (comma != std::string::npos) {
			nodeName = file.substr(comma + 1);
			nodeName = nodeName.substr(0, nodeName.find(','));
			file = file.substr(0, comma);
		}
		if (!endsWith(file, ".hgr")) file += ".hgr";
		const std::string path = modelDir + file;
		try {
			P(hgr::Scene) model = new hgr::Scene(ctx, path.c_str(), resources, options.textureDir().c_str(), options.shaderDir().c_str(),
			                                     options.particleDir().c_str());
			hgr::Node* named = nodeName.empty() ? 0 : model->findNodeByName(nodeName.c_str());
			const std::string markerProps = props->containsKey(request.first.c_str()) ? props->get(request.first.c_str()).c_str() : "";
			const bool noShadow = markerProps.find("shadow=0") != std::string::npos;
			if (named) {
				P(hgr::Node) keep = named;
				hgr::Node* parent = marker->parent() ? marker->parent() : (hgr::Node*)scene;
				if (!noShadow) moveShadowsInto(model, named);
				named->setTransform(marker->transform());
				named->linkTo(parent);
				marker->setName(("T_" + request.first).c_str());
				named->setName(request.first.c_str());
				if (noShadow) disableShadows(named);
				LibraryObject object;
				object.marker = request.first;
				object.visual = named;
				object.properties = markerProps;
				if (hgr::UserPropertySet* modelProps = model->userProperties()) {
					if (modelProps->containsKey(nodeName.c_str())) object.properties += std::string("\n") + modelProps->get(nodeName.c_str()).c_str();
					if (object.properties.find("a01") == std::string::npos)
						for (lang::HashtableIterator<lang::String, lang::String> mi = modelProps->begin(); mi != modelProps->end(); ++mi) {
							object.properties += std::string("\n") + mi.value().c_str();
							if (std::string(mi.value().c_str()).find("a01") != std::string::npos)
								object.trigger = std::string(mi.key().c_str()) + "_" + request.first;
						}
				}
				if (object.trigger.empty()) object.trigger = "T_" + request.first;
				objects.push_back(object);
			} else {
				std::vector<P(hgr::Node)> children;
				for (hgr::Node* c = model->firstChild(); c; c = c->next(model))
					if (c->parent() == (hgr::Node*)model) children.push_back(c);
				for (auto& c : children) c->linkTo(marker);
			}
			++loaded;
		} catch (lang::Throwable& e) {
			fprintf(stderr, "library object '%s' (%s): %s\n", request.first.c_str(), path.c_str(), e.getMessage().format().c_str());
		}
	}
	printf("library objects: %d of %d loaded\n", loaded, (int)requests.size());
	if (traceEnabled())
		for (const auto& request : requests) printf("[trace] library object %s -> %s\n", request.first.c_str(), request.second.c_str());
	return loaded;
}

std::vector<hgr::Camera*> collectCameras(hgr::Node* root) {
	std::vector<hgr::Camera*> cameras;
	for (hgr::Node* n = root->firstChild(); n; n = n->next(root))
		if (n->classId() == hgr::Node::NODE_CAMERA) cameras.push_back(static_cast<hgr::Camera*>(n));
	return cameras;
}

bool linearFog(hgr::Scene* scene, float& start, float& end, float color[3]) {
	if (scene->fogType() != hgr::Scene::FOG_LINEAR || scene->fogEnd() <= scene->fogStart()) return false;
	start = scene->fogStart();
	end = scene->fogEnd();
	color[0] = scene->fogColor().x;
	color[1] = scene->fogColor().y;
	color[2] = scene->fogColor().z;
	printf("fog: linear from %g to %g\n", start, end);
	return true;
}

unsigned backgroundColor(hgr::Scene* scene) {
	const std::string color = sceneProperty(scene, "D_start", "bgcolor");
	if (color.size() >= 6) return 0xFF000000u | (unsigned)strtoul(color.substr(0, 6).c_str(), 0, 16);
	return 0xFF6688AAu;
}

}
