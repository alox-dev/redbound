#pragma once
#include <lang/Object.h>
#include <string>
#include <vector>

namespace gr { class Context; }
namespace hgr { class Scene; class Node; class Camera; class ResourceManager; }

namespace redbound {

struct LaunchOptions;

struct LibraryObject {
	std::string marker;
	P(hgr::Node) visual;
	std::string properties;
	std::string trigger;
};

std::vector<hgr::Node*> hideHelperNodes(hgr::Scene* scene);
int loadLibraryObjects(hgr::Scene* scene, gr::Context* ctx, hgr::ResourceManager* resources, const LaunchOptions& options,
                       std::vector<LibraryObject>& objects);
std::vector<hgr::Camera*> collectCameras(hgr::Node* root);
bool linearFog(hgr::Scene* scene, float& start, float& end, float color[3]);
unsigned backgroundColor(hgr::Scene* scene);
std::string sceneProperty(hgr::Scene* scene, const std::string& node, const std::string& key);

}
