#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace redbound {

enum class TexelFormat { RGBA8, RGB565, RGBA4 };

struct RgbaLevel {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba;
};

int picaTextureSize(int size, int maxSize);
TexelFormat chooseTexelFormat(bool hasAlpha);
size_t texelBytes(TexelFormat format);
uint32_t mortonIndex(int x, int y);
RgbaLevel resampleRgba(const uint8_t* rgba, int width, int height, int newWidth, int newHeight);
RgbaLevel halveRgba(const RgbaLevel& level);
int picaMipLevels(int width, int height);
void tileLevel(const uint8_t* rgba, int width, int height, TexelFormat format, uint8_t* out);

}
