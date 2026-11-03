#pragma once
#include <map>
#include <string>

namespace redbound {

class Config {
public:
	static Config& get();
	void load();
	void save() const;
	std::string str(const std::string& k, const std::string& def = "") const;
	bool flag(const std::string& k, bool def = false) const;
	float num(const std::string& k, float def = 0.f) const;
	void set(const std::string& k, const std::string& v) { m_v[k] = v; }
	void setFlag(const std::string& k, bool v) { m_v[k] = v ? "1" : "0"; }
	void setNum(const std::string& k, float v);
	std::string path() const;
	void applyToEnvironment() const;
private:
	std::map<std::string, std::string> m_v;
};

}
