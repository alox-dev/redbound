#include "EffectParser.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace redbound {

std::string dirOf(const std::string& path) {
	size_t i = path.find_last_of("/\\");
	return i == std::string::npos ? std::string() : path.substr(0, i + 1);
}

std::string joinPath(const std::string& dir, const std::string& rel) {
	std::string r = rel;
	std::replace(r.begin(), r.end(), '\\', '/');
	if (!r.empty() && r[0] == '/') return r;
	return dir + r;
}

static bool readText(const std::string& path, std::string& out) {
	std::ifstream f(path.c_str(), std::ios::binary);
	if (!f) return false;
	std::stringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
	return true;
}

static std::string trim(const std::string& s) {
	size_t a = s.find_first_not_of(" \t\n");
	if (a == std::string::npos) return std::string();
	size_t b = s.find_last_not_of(" \t\n");
	return s.substr(a, b - a + 1);
}

static std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

namespace {

struct Pre {
	std::map<std::string, std::string> macros;
	std::vector<std::string> order;
	std::string error;

	const char* p;
	bool expr() { bool v = andExpr(); while (skip(), p[0] == '|' && p[1] == '|') { p += 2; bool r = andExpr(); v = v || r; } return v; }
	bool andExpr() { bool v = unary(); while (skip(), p[0] == '&' && p[1] == '&') { p += 2; bool r = unary(); v = v && r; } return v; }
	bool unary() {
		skip();
		if (*p == '!') { ++p; return !unary(); }
		if (*p == '(') { ++p; bool v = expr(); skip(); if (*p == ')') ++p; return v; }
		std::string id = ident();
		if (id == "defined") {
			skip();
			bool paren = *p == '(';
			if (paren) ++p;
			skip();
			std::string name = ident();
			skip();
			if (paren && *p == ')') ++p;
			return macros.count(name) != 0;
		}
		if (!id.empty() && isdigit((unsigned char)id[0])) return atoi(id.c_str()) != 0;
		if (macros.count(id)) return atoi(macros[id].c_str()) != 0;
		return false;
	}
	void skip() { while (*p == ' ' || *p == '\t') ++p; }
	std::string ident() { skip(); const char* s = p; while (isalnum((unsigned char)*p) || *p == '_') ++p; return std::string(s, p); }
	bool evalIf(const std::string& e) { p = e.c_str(); return expr(); }

	bool run(const std::string& path, std::string& out, int depth) {
		if (depth > 16) { error = "demasiados #include anidados en " + path; return false; }
		std::string text;
		if (!readText(path, text)) { error = "could not open " + path; return false; }
		std::istringstream in(text);
		std::string line;
		std::vector<std::pair<bool, bool> > stack;
		while (std::getline(in, line)) {
			std::string t = trim(line);
			bool active = stack.empty() || stack.back().first;
			if (!t.empty() && t[0] == '#') {
				std::istringstream ls(t.substr(1));
				std::string dir;
				ls >> dir;
				std::string rest;
				std::getline(ls, rest);
				rest = trim(rest);
				bool parent = true;
				for (size_t i = 0; i + 1 < stack.size(); ++i) parent = parent && stack[i].first;
				if (dir == "ifdef" || dir == "ifndef" || dir == "if") {
					bool outer = active;
					bool c = dir == "ifdef" ? macros.count(rest) != 0 : dir == "ifndef" ? macros.count(rest) == 0 : evalIf(rest);
					stack.push_back(std::make_pair(outer && c, c));
				} else if (dir == "elif") {
					if (stack.empty()) continue;
					bool c = !stack.back().second && evalIf(rest);
					stack.back().first = parent && c;
					stack.back().second = stack.back().second || c;
				} else if (dir == "else") {
					if (stack.empty()) continue;
					stack.back().first = parent && !stack.back().second;
					stack.back().second = true;
				} else if (dir == "endif") {
					if (!stack.empty()) stack.pop_back();
				} else if (!active) {
					continue;
				} else if (dir == "define") {
					std::istringstream ds(rest);
					std::string name, value;
					ds >> name;
					std::getline(ds, value);
					if (!macros.count(name)) order.push_back(name);
					macros[name] = trim(value);
				} else if (dir == "undef") {
					macros.erase(rest);
					order.erase(std::remove(order.begin(), order.end(), rest), order.end());
				} else if (dir == "include") {
					std::string f = rest;
					f.erase(std::remove(f.begin(), f.end(), '"'), f.end());
					f.erase(std::remove(f.begin(), f.end(), '<'), f.end());
					f.erase(std::remove(f.begin(), f.end(), '>'), f.end());
					if (!run(joinPath(dirOf(path), trim(f)), out, depth + 1)) return false;
				}
				continue;
			}
			if (active) { out += line; out += '\n'; }
		}
		return true;
	}
};

struct Lexer {
	std::string s;
	size_t i = 0;
	void ws() {
		for (;;) {
			while (i < s.size() && isspace((unsigned char)s[i])) ++i;
			if (s.compare(i, 2, "//") == 0) { while (i < s.size() && s[i] != '\n') ++i; continue; }
			if (s.compare(i, 2, "/*") == 0) { size_t e = s.find("*/", i + 2); i = e == std::string::npos ? s.size() : e + 2; continue; }
			break;
		}
	}
	std::string next() {
		ws();
		if (i >= s.size()) return std::string();
		char c = s[i];
		if (c == '{' || c == '}' || c == '=' || c == ';') { ++i; return std::string(1, c); }
		size_t b = i;
		while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '{' && s[i] != '}' && s[i] != '=' && s[i] != ';') ++i;
		return s.substr(b, i - b);
	}
	std::string value() {
		ws();
		size_t e = s.find(';', i);
		if (e == std::string::npos) e = s.size();
		std::string v = trim(s.substr(i, e - i));
		i = e < s.size() ? e + 1 : e;
		return v;
	}
};

}

bool parseEffect(const std::string& path, const std::vector<std::string>& extraDefines, Effect& out) {
	out = Effect();
	out.path = path;
	Pre pre;
	for (size_t i = 0; i < extraDefines.size(); ++i) {
		std::istringstream ds(extraDefines[i]);
		std::string n, v;
		ds >> n;
		std::getline(ds, v);
		pre.macros[n] = trim(v);
		pre.order.push_back(n);
	}
	std::string text;
	if (!pre.run(path, text, 0)) { out.error = pre.error; return false; }
	for (size_t i = 0; i < pre.order.size(); ++i) {
		const std::string& v = pre.macros[pre.order[i]];
		out.defines.push_back(v.empty() ? pre.order[i] : pre.order[i] + " " + v);
	}

	const std::string base = dirOf(path);
	Lexer lx;
	lx.s = text;
	for (;;) {
		std::string tok = lx.next();
		if (tok.empty()) break;
		if (tok == "technique") {
			EffectTechnique tech;
			tech.name = lx.next();
			if (lx.next() != "{") { out.error = "expected '{' after technique " + tech.name; return false; }
			for (;;) {
				std::string t = lx.next();
				if (t == "}" || t.empty()) break;
				if (t != "pass") { out.error = "expected 'pass' in technique " + tech.name + ", vino '" + t + "'"; return false; }
				EffectPass pass;
				pass.name = lx.next();
				if (lx.next() != "{") { out.error = "expected '{' after pass " + pass.name; return false; }
				for (;;) {
					std::string key = lx.next();
					if (key == "}" || key.empty()) break;
					if (lx.next() != "=") { out.error = "expected '=' after " + key; return false; }
					std::string val = lx.value();
					std::string k = lower(key);
					if (k == "vertexshader") pass.vertexShaderPath = joinPath(base, val);
					else if (k == "pixelshader" || k == "fragmentshader") pass.pixelShaderPath = joinPath(base, val);
					else pass.states[k] = val;
				}
				tech.passes.push_back(pass);
			}
			out.techniques.push_back(tech);
		} else {
			if (lx.next() != "=") { out.error = "unexpected syntax near '" + tok + "'"; return false; }
			out.globals[lower(tok)] = lx.value();
		}
	}
	if (out.techniques.empty()) { out.error = "the effect defines no technique"; return false; }
	return true;
}

static bool expandIncludes(const std::string& path, std::string& out, std::set<std::string>& seen, std::string& error, int depth) {
	if (depth > 16) { error = "demasiados #include anidados en " + path; return false; }
	std::string text;
	if (!readText(path, text)) { error = "could not open " + path; return false; }
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		std::string t = trim(line);
		if (t.compare(0, 8, "#include") == 0) {
			std::string f = trim(t.substr(8));
			f.erase(std::remove(f.begin(), f.end(), '"'), f.end());
			std::string inc = joinPath(dirOf(path), f);
			if (seen.insert(inc).second && !expandIncludes(inc, out, seen, error, depth + 1)) return false;
			continue;
		}
		out += line;
		out += '\n';
	}
	return true;
}

bool buildShaderSource(const std::string& path, const std::vector<std::string>& defines, bool fragment,
                       std::string& source, std::string& error) {
	std::string body;
	std::set<std::string> seen;
	if (!expandIncludes(path, body, seen, error, 0)) return false;
	source = "#version 100\n";
	if (fragment) source += "precision mediump float;\n";
	for (size_t i = 0; i < defines.size(); ++i) source += "#define " + defines[i] + "\n";
	source += "#line 1\n";
	source += body;
	return true;
}

}
