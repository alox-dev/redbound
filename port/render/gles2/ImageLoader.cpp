#include "ImageLoader.h"
#include "PVRTDecompress.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace redbound {

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? (size_t)n : 0);
	bool ok = n <= 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	return ok;
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

enum {
	MGL_ARGB4444 = 0x00, MGL_ARGB1555 = 0x01, MGL_RGB565 = 0x02, MGL_RGB555 = 0x03, MGL_RGB888 = 0x04,
	MGL_ARGB8888 = 0x05, MGL_ARGB8332 = 0x06, MGL_I8 = 0x07, MGL_AI88 = 0x08, MGL_1BPP = 0x09,
	MGL_VY1UY0 = 0x0A, MGL_Y1VY0U = 0x0B, MGL_PVRTC2 = 0x0C, MGL_PVRTC4 = 0x0D,
	OGL_RGBA4444 = 0x10, OGL_RGBA5551 = 0x11, OGL_RGBA8888 = 0x12, OGL_RGB565 = 0x13, OGL_RGB555 = 0x14,
	OGL_RGB888 = 0x15, OGL_I8 = 0x16, OGL_AI88 = 0x17, OGL_PVRTC2 = 0x18, OGL_PVRTC4 = 0x19, OGL_BGRA8888 = 0x1A, OGL_A8 = 0x1B
};
enum { PVR_FLAG_MIPMAP = 0x100, PVR_FLAG_TWIDDLE = 0x200, PVR_FLAG_ALPHA = 0x8000, PVR_FLAG_VFLIP = 0x10000 };

static uint8_t x4(unsigned v) { return (uint8_t)(v * 17); }
static uint8_t x5(unsigned v) { return (uint8_t)((v << 3) | (v >> 2)); }
static uint8_t x6(unsigned v) { return (uint8_t)((v << 2) | (v >> 4)); }

static size_t decodeUncompressed(int fmt, const uint8_t* src, size_t avail, int w, int h, uint8_t* dst) {
	const int n = w * h;
	int bpp = 0;
	switch (fmt) {
	case OGL_RGBA4444: case OGL_RGBA5551: case OGL_RGB565: case OGL_RGB555: case OGL_AI88:
	case MGL_ARGB4444: case MGL_ARGB1555: case MGL_RGB565: case MGL_RGB555: case MGL_AI88: bpp = 2; break;
	case OGL_RGBA8888: case OGL_BGRA8888: case MGL_ARGB8888: bpp = 4; break;
	case OGL_RGB888: case MGL_RGB888: bpp = 3; break;
	case OGL_I8: case OGL_A8: case MGL_I8: bpp = 1; break;
	default: return 0;
	}
	if ((size_t)n * bpp > avail) return 0;
	for (int i = 0; i < n; ++i) {
		const uint8_t* s = src + (size_t)i * bpp;
		uint8_t* d = dst + (size_t)i * 4;
		unsigned v = bpp == 2 ? (unsigned)(s[0] | (s[1] << 8)) : 0;
		switch (fmt) {
		case OGL_RGBA4444: d[0] = x4(v >> 12); d[1] = x4((v >> 8) & 15); d[2] = x4((v >> 4) & 15); d[3] = x4(v & 15); break;
		case MGL_ARGB4444: d[3] = x4(v >> 12); d[0] = x4((v >> 8) & 15); d[1] = x4((v >> 4) & 15); d[2] = x4(v & 15); break;
		case OGL_RGBA5551: d[0] = x5(v >> 11); d[1] = x5((v >> 6) & 31); d[2] = x5((v >> 1) & 31); d[3] = (v & 1) ? 255 : 0; break;
		case MGL_ARGB1555: d[3] = (v & 0x8000) ? 255 : 0; d[0] = x5((v >> 10) & 31); d[1] = x5((v >> 5) & 31); d[2] = x5(v & 31); break;
		case OGL_RGB565: case MGL_RGB565: d[0] = x5(v >> 11); d[1] = x6((v >> 5) & 63); d[2] = x5(v & 31); d[3] = 255; break;
		case OGL_RGB555: case MGL_RGB555: d[0] = x5((v >> 10) & 31); d[1] = x5((v >> 5) & 31); d[2] = x5(v & 31); d[3] = 255; break;
		case OGL_AI88: case MGL_AI88: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; break;
		case OGL_RGBA8888: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; break;
		case OGL_BGRA8888: case MGL_ARGB8888: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; break;
		case OGL_RGB888: case MGL_RGB888: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
		case OGL_I8: case MGL_I8: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
		case OGL_A8: d[0] = d[1] = d[2] = 255; d[3] = s[0]; break;
		}
	}
	return (size_t)n * bpp;
}

static bool loadPVRLegacy(const std::vector<uint8_t>& file, Image& img) {
	const uint8_t* p = &file[0];
	const uint32_t headerSize = rd32(p + 0);
	const int height = (int)rd32(p + 4), width = (int)rd32(p + 8);
	const uint32_t mipCount = rd32(p + 12), flags = rd32(p + 16);
	const int fmt = flags & 0xFF;
	if (headerSize > file.size() || width <= 0 || height <= 0) { img.error = "invalid PVR header"; return false; }

	const bool pvrtc = fmt == OGL_PVRTC2 || fmt == OGL_PVRTC4 || fmt == MGL_PVRTC2 || fmt == MGL_PVRTC4;
	const bool twoBit = fmt == OGL_PVRTC2 || fmt == MGL_PVRTC2;
	img.hasAlpha = (flags & PVR_FLAG_ALPHA) != 0 || rd32(p + 40) != 0;
	img.verticalFlipFlag = (flags & PVR_FLAG_VFLIP) != 0;
	if (!pvrtc && (flags & PVR_FLAG_TWIDDLE)) { img.error = "uncompressed twiddled PVR not supported"; return false; }

	size_t off = headerSize;
	int w = width, h = height;
	const int levels = 1 + ((flags & PVR_FLAG_MIPMAP) ? (int)mipCount : 0);
	for (int l = 0; l < levels; ++l) {
		ImageLevel lev;
		lev.width = w; lev.height = h;
		lev.rgba.resize((size_t)w * h * 4);
		size_t used;
		if (pvrtc) {
			const int bw = twoBit ? std::max(w, 16) : std::max(w, 8);
			const int bh = std::max(h, 8);
			used = (size_t)bw * bh * (twoBit ? 2 : 4) / 8;
			if (off + used > file.size()) break;
			pvr::PVRTDecompressPVRTC(&file[off], twoBit ? 1 : 0, (uint32_t)w, (uint32_t)h, &lev.rgba[0]);
		} else {
			used = decodeUncompressed(fmt, &file[off], file.size() - off, w, h, &lev.rgba[0]);
			if (!used) {
				if (l == 0) { char b[64]; snprintf(b, sizeof(b), "PVR format 0x%02x not supported", fmt); img.error = b; return false; }
				break;
			}
		}
		off += used;
		img.levels.push_back(lev);
		w = std::max(1, w / 2); h = std::max(1, h / 2);
	}
	if (img.levels.empty()) { img.error = "PVR without data"; return false; }
	return true;
}

bool loadImage(const std::string& path, Image& img) {
	img = Image();
	std::vector<uint8_t> file;
	if (!readFile(path, file)) { img.error = "could not open " + path; return false; }

	const std::string ext = lower(path.size() > 4 ? path.substr(path.size() - 4) : "");
	if (file.size() >= 52 && memcmp(&file[44], "PVR!", 4) == 0)
		return loadPVRLegacy(file, img);
	if (file.size() >= 4 && rd32(&file[0]) == 0x03525650) { img.error = "PVR v3 not supported yet"; return false; }
	if (ext == ".pvr") { img.error = ".pvr file without a known signature"; return false; }

	int w, h, comp;
	stbi_uc* data = stbi_load_from_memory(&file[0], (int)file.size(), &w, &h, &comp, 4);
	if (!data) { img.error = std::string("stb_image: ") + stbi_failure_reason(); return false; }
	ImageLevel lev;
	lev.width = w; lev.height = h;
	lev.rgba.assign(data, data + (size_t)w * h * 4);
	stbi_image_free(data);
	img.hasAlpha = comp == 2 || comp == 4;
	img.levels.push_back(lev);
	return true;
}

}
