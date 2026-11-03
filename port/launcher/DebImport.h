#pragma once
#include <functional>
#include <string>
#include <vector>

namespace redbound {

struct DebImportResult {
	bool ok = false;
	std::string error;
	int files = 0;
	long long bytes = 0;
	std::string sha256;
	bool knownVersion = false;
};

DebImportResult importDeb(const std::string& debPath, const std::string& destDir,
                          std::function<void(const std::string&, int)> progress = nullptr);

bool gameDataPresent(const std::string& destDir);

const std::vector<std::string>& supportedPackageHashes();

std::string defaultGameDir();

}
