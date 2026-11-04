#pragma once
#include <string>

namespace redbound {

bool makeDirectory(const std::string& path);
void makeDirectories(const std::string& path);
std::string userDataDir();
std::string userConfigDir();
void setEnvironment(const char* name, const char* value);

}
