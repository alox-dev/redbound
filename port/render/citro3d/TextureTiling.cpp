#include "TextureTiling.h"
#include <algorithm>

namespace redbound {

int picaTextureSize(int size, int maxSize) {
	int s = 8;
	while (s < size && s < maxSize) s <<= 1;
	return s;
}

TexelFormat chooseTexelFormat(bool hasAlpha) {
	return hasAlpha ? TexelFormat::RGBA8 : TexelFormat::RGB565;
}

size_t texelBytes(TexelFormat format) {
	return format == TexelFormat::RGBA8 ? 4 : 2;
}

uint32_t mortonIndex(int x, int y) {
	return (uint32_t)((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
}

RgbaLevel resampleRgba(const uint8_t* rgba, int width, int height, int newWidth, int newHeight) {
	RgbaLevel out;
	out.width = newWidth;
	out.height = newHeight;
	out.rgba.resize((size_t)newWidth * newHeight * 4);
	for (int y = 0; y < newHeight; ++y) {
		const int y0 = y * height / newHeight;
		const int y1 = std::max(y0 + 1, (y + 1) * height / newHeight);
		for (int x = 0; x < newWidth; ++x) {
			const int x0 = x * width / newWidth;
			const int x1 = std::max(x0 + 1, (x + 1) * width / newWidth);
			unsigned sum[4] = { 0, 0, 0, 0 };
			for (int sy = y0; sy < y1; ++sy)
				for (int sx = x0; sx < x1; ++sx)
					for (int c = 0; c < 4; ++c) sum[c] += rgba[((size_t)sy * width + sx) * 4 + c];
			const unsigned n = (unsigned)((y1 - y0) * (x1 - x0));
			for (int c = 0; c < 4; ++c) out.rgba[((size_t)y * newWidth + x) * 4 + c] = (uint8_t)((sum[c] + n / 2) / n);
		}
	}
	return out;
}

RgbaLevel halveRgba(const RgbaLevel& level) {
	return resampleRgba(&level.rgba[0], level.width, level.height, std::max(1, level.width / 2), std::max(1, level.height / 2));
}

int picaMipLevels(int width, int height) {
	int levels = 1;
	int m = std::min(width, height);
	while (m > 8) {
		m >>= 1;
		++levels;
	}
	return levels;
}

void tileLevel(const uint8_t* rgba, int width, int height, TexelFormat format, uint8_t* out) {
	const size_t bpp = texelBytes(format);
	const int tilesPerRow = width / 8;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const uint8_t* p = rgba + ((size_t)y * width + x) * 4;
			const size_t index = ((size_t)(y / 8) * tilesPerRow + (x / 8)) * 64 + mortonIndex(x & 7, y & 7);
			uint8_t* d = out + index * bpp;
			switch (format) {
			case TexelFormat::RGBA8:
				d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0];
				break;
			case TexelFormat::RGB565: {
				const uint16_t v = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
				d[0] = (uint8_t)(v & 0xFF); d[1] = (uint8_t)(v >> 8);
				break;
			}
			case TexelFormat::RGBA4: {
				const uint16_t v = (uint16_t)(((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | (p[3] >> 4));
				d[0] = (uint8_t)(v & 0xFF); d[1] = (uint8_t)(v >> 8);
				break;
			}
			}
		}
	}
}

}
