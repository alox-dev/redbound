#pragma once
#include <string>

namespace redbound {

bool isDirectory(const std::string& path);
bool fileExists(const std::string& path);
std::string parentDirectory(const std::string& path);
std::string withTrailingSlash(std::string path);
std::string toLower(std::string s);
bool endsWith(const std::string& s, const std::string& suffix);
void makeDirectories(const std::string& path);

}
