#include "DebImport.h"
#include "Sha256.h"
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <vector>

namespace redbound {

static bool readAll(const std::string& p, std::vector<unsigned char>& out) {
	FILE* f = fopen(p.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0) { fclose(f); return false; }
	out.resize((size_t)n);
	const bool ok = n == 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	return ok;
}

static bool mkdirs(const std::string& path) {
	std::string cur;
	for (size_t i = 0; i < path.size(); ++i) {
		cur += path[i];
		if (path[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
	}
	mkdir(path.c_str(), 0755);
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool gunzip(const unsigned char* in, size_t n, std::vector<unsigned char>& out, std::string& err) {
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) { err = "zlib could not be initialized"; return false; }
	zs.next_in = const_cast<unsigned char*>(in);
	zs.avail_in = (uInt)n;
	out.clear();
	unsigned char buf[1 << 16];
	int r;
	do {
		zs.next_out = buf;
		zs.avail_out = sizeof(buf);
		r = inflate(&zs, Z_NO_FLUSH);
		if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&zs); err = "corrupted compressed data"; return false; }
		out.insert(out.end(), buf, buf + (sizeof(buf) - zs.avail_out));
	} while (r != Z_STREAM_END);
	inflateEnd(&zs);
	return true;
}

static long long octal(const char* p, size_t n) {
	long long v = 0;
	for (size_t i = 0; i < n && p[i]; ++i) {
		if (p[i] == ' ') continue;
		if (p[i] < '0' || p[i] > '7') break;
		v = v * 8 + (p[i] - '0');
	}
	return v;
}

static std::string cstr(const char* p, size_t n) { return std::string(p, strnlen(p, n)); }

DebImportResult importDeb(const std::string& debPath, const std::string& destDir, std::function<void(const std::string&, int)> progress) {
	DebImportResult res;
	std::vector<unsigned char> deb;
	if (!readAll(debPath, deb)) { res.error = "Could not read the file."; return res; }
	if (deb.size() < 8 || memcmp(&deb[0], "!<arch>\n", 8) != 0) { res.error = "The file is not a .deb package."; return res; }
	res.sha256 = sha256Hex(&deb[0], deb.size());
	for (const std::string& known : supportedPackageHashes()) res.knownVersion = res.knownVersion || known == res.sha256;
	size_t off = 8;
	std::vector<unsigned char> tar;
	bool found = false;
	while (off + 60 <= deb.size()) {
		const char* h = (const char*)&deb[off];
		std::string name = cstr(h, 16);
		while (!name.empty() && (name.back() == ' ' || name.back() == '/')) name.pop_back();
		const long long size = atoll(std::string(h + 48, 10).c_str());
		const size_t data = off + 60;
		if (size < 0 || data + (size_t)size > deb.size()) { res.error = "The package is incomplete or damaged."; return res; }
		if (name.compare(0, 8, "data.tar") == 0) {
			found = true;
			if (name == "data.tar") tar.assign(deb.begin() + data, deb.begin() + data + size);
			else if (name == "data.tar.gz") {
				if (!gunzip(&deb[data], (size_t)size, tar, res.error)) return res;
			} else {
				res.error = "Unsupported compression (" + name + "); expected data.tar.gz.";
				return res;
			}
			break;
		}
		off = data + (size_t)size + ((size_t)size & 1);
	}
	if (!found) { res.error = "The package contains no data (data.tar)."; return res; }

	if (!mkdirs(destDir)) { res.error = "Could not create the folder " + destDir + "."; return res; }
	size_t p = 0;
	std::string longName;
	while (p + 512 <= tar.size()) {
		const char* h = (const char*)&tar[p];
		if (h[0] == 0) break;
		std::string name = cstr(h, 100);
		const std::string prefix = memcmp(h + 257, "ustar", 5) == 0 ? cstr(h + 345, 155) : std::string();
		if (!prefix.empty()) name = prefix + "/" + name;
		const long long size = octal(h + 124, 12);
		const char type = h[156];
		const size_t body = p + 512;
		if (body + (size_t)size > tar.size()) { res.error = "The package data is incomplete."; return res; }
		if (type == 'L') { longName = cstr((const char*)&tar[body], (size_t)size); p = body + ((size + 511) / 512) * 512; continue; }
		if (!longName.empty()) { name = longName; longName.clear(); }
		while (name.compare(0, 2, "./") == 0) name = name.substr(2);
		const std::string root = "opt/bounce/data";
		if (name.compare(0, root.size(), root) == 0 && name.find("..") == std::string::npos) {
			const std::string rel = name.substr(std::string("opt/bounce/").size());
			const std::string out = destDir + "/" + rel;
			if (type == '5') mkdirs(out);
			else if (type == '0' || type == 0) {
				const size_t slash = out.find_last_of('/');
				if (slash != std::string::npos) mkdirs(out.substr(0, slash));
				FILE* f = fopen(out.c_str(), "wb");
				if (!f) { res.error = "Could not write " + out + "."; return res; }
				if (size > 0) fwrite(&tar[body], 1, (size_t)size, f);
				fclose(f);
				++res.files;
				res.bytes += size;
				if (progress) progress(rel, res.files);
			}
		}
		p = body + ((size + 511) / 512) * 512;
	}
	if (!gameDataPresent(destDir)) {
		res.error = "The package does not look like Bounce Evolution (game data missing).";
		return res;
	}
	res.ok = true;
	return res;
}

const std::vector<std::string>& supportedPackageHashes() {
	static const std::vector<std::string> hashes = {
		"09e0b3abc8c422ffaa3a2fca7e796749d6ad6797137b840d84e28938c6401ab4",
	};
	return hashes;
}

bool gameDataPresent(const std::string& destDir) {
	const char* need[] = { "/data/bundles/original/level01.hgr", "/data/resources/models/bounce.hgr", "/data/resources/images/MENU.dat" };
	for (const char* n : need) {
		struct stat st;
		if (stat((destDir + n).c_str(), &st) != 0) return false;
	}
	return true;
}

std::string defaultGameDir() {
	const char* x = getenv("XDG_DATA_HOME");
	const char* home = getenv("HOME");
	return (x && *x ? std::string(x) : std::string(home ? home : ".") + "/.local/share") + "/redbound";
}

}
