#include "ImageLoader.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <string>

static bool convert(const std::string& in, const std::string& out, bool mips) {
	redbound::Image img;
	if (!redbound::loadImage(in, img)) { fprintf(stderr, "%s: %s\n", in.c_str(), img.error.c_str()); return false; }
	const size_t n = mips ? img.levels.size() : 1;
	for (size_t l = 0; l < n; ++l) {
		const redbound::ImageLevel& lv = img.levels[l];
		std::string name = out;
		if (l > 0) { char b[32]; snprintf(b, sizeof(b), ".mip%zu", l); name = out.substr(0, out.size() - 4) + b + ".png"; }
		if (!stbi_write_png(name.c_str(), lv.width, lv.height, 4, &lv.rgba[0], lv.width * 4)) { fprintf(stderr, "could not write %s\n", name.c_str()); return false; }
	}
	const redbound::ImageLevel& b = img.levels[0];
	double sum[3] = { 0, 0, 0 };
	for (size_t i = 0; i < b.rgba.size(); i += 4) for (int c = 0; c < 3; ++c) sum[c] += b.rgba[i + c];
	const double px = (double)b.width * b.height;
	printf("%s -> %s (%dx%d, %zu niveles, alfa %d, volteo %d, promedio RGB %.0f %.0f %.0f)\n", in.c_str(), out.c_str(),
	       b.width, b.height, img.levels.size(), img.hasAlpha, img.verticalFlipFlag, sum[0] / px, sum[1] / px, sum[2] / px);
	return true;
}

int main(int argc, char** argv) {
	bool mips = false;
	int a = 1;
	if (a < argc && !strcmp(argv[a], "--mips")) { mips = true; ++a; }
	if (a < argc && !strcmp(argv[a], "--dir") && a + 2 < argc) {
		std::string in = argv[a + 1], out = argv[a + 2];
		DIR* d = opendir(in.c_str());
		if (!d) { fprintf(stderr, "cannot open %s\n", in.c_str()); return 1; }
		int ok = 0, fail = 0;
		while (dirent* e = readdir(d)) {
			std::string f = e->d_name;
			if (f.size() < 5 || f.substr(f.size() - 4) != ".pvr") continue;
			if (convert(in + "/" + f, out + "/" + f.substr(0, f.size() - 4) + ".png", mips)) ++ok; else ++fail;
		}
		closedir(d);
		printf("%d converted, %d failed\n", ok, fail);
		return fail ? 2 : 0;
	}
	if (a >= argc) { fprintf(stderr, "usage: %s [--mips] input.pvr [output.png]\n       %s [--mips] --dir input output\n", argv[0], argv[0]); return 1; }
	std::string in = argv[a];
	std::string out = a + 1 < argc ? argv[a + 1] : in.substr(0, in.find_last_of('.')) + ".png";
	return convert(in, out, mips) ? 0 : 2;
}
