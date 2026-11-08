#pragma once
#include <string>

namespace redbound {

bool androidPickDeb();
bool androidPicking();
std::string androidTakePicked();
std::string androidExternalDir();
std::string androidResolveUrl(const std::string& url);
void androidSetOrientationLocked(bool locked);

}
