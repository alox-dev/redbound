#pragma once
#include <string>

namespace redbound {

enum class UpdateState { Idle, Checking, UpToDate, Available, Failed };

void startUpdateCheck(const std::string& currentVersion);
UpdateState updateState();
std::string latestVersion();
std::string latestReleaseUrl();
bool isNewerVersion(const std::string& candidate, const std::string& current);

}
