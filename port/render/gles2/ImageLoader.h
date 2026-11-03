#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace redbound {

struct ImageLevel {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba;
};

struct Image {
	std::vector<ImageLevel> levels;
	bool hasAlpha = false;
	bool verticalFlipFlag = false;
	std::string error;
};

bool loadImage(const std::string& path, Image& img);

}
