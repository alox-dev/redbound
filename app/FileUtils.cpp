#include "FileUtils.h"
#include <cctype>
#include <sys/stat.h>

namespace redbound {

bool isDirectory(const std::string& path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileExists(const std::string& path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

std::string parentDirectory(const std::string& path) {
	const size_t i = path.find_last_of('/');
	return i == std::string::npos ? std::string(".") : path.substr(0, i);
}

std::string withTrailingSlash(std::string path) {
	if (!path.empty() && path.back() != '/') path += '/';
	return path;
}

std::string toLower(std::string s) {
	for (char& c : s) c = (char)tolower((unsigned char)c);
	return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void makeDirectories(const std::string& path) {
	std::string current;
	for (char c : path) {
		current += c;
		if (c == '/') mkdir(current.c_str(), 0755);
	}
	mkdir(path.c_str(), 0755);
}

}
