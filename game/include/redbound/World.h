#pragma once
#include <string>
#include <vector>

namespace redbound {

class World {
public:
	virtual ~World() = default;

	virtual float getVariable(const std::string& name) const = 0;
	virtual void setVariable(const std::string& name, float value) = 0;
	virtual void setPersistentVariable(const std::string& name, float value) = 0;

	virtual void setTriggerEnabled(const std::string& trigger, bool enabled) = 0;
	virtual void activateTrigger(const std::string& trigger) = 0;

	virtual void link(const std::string& parent, const std::vector<std::string>& nodes) = 0;
	virtual void unlink(const std::vector<std::string>& nodes) = 0;

	virtual int playAnimation(const std::string& node, const std::string& anim, float start, float end, bool loop) = 0;
	virtual bool animationDone(int id) const = 0;

	enum TransformKind { MOVE, ROTATE, SCALE, UV };
	virtual int startTransform(TransformKind kind, const std::string& object, const std::string& subnode,
	                           float x, float y, float z, float duration) = 0;
	virtual bool transformDone(int id) const = 0;

	virtual void setFade(float amount, int color) = 0;

	virtual bool isKeyDown(const std::string& key) const = 0;

	virtual bool genericAction(const std::string& name, const std::vector<std::string>& args) = 0;
};

}
