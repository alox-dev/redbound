#include "DebImport.h"
#include "Sha256.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace redbound;
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(int argc, char** argv) {
	const std::string work = std::string(argc > 1 ? argv[1] : "/tmp") + "/debtest";
	if (system(("rm -rf '" + work + "' && mkdir -p '" + work + "'").c_str()) != 0) return 1;
	if (system("which dpkg-deb >/dev/null 2>&1") != 0) { printf("no dpkg-deb: test skipped\n"); return 0; }
	const std::string pkg = work + "/pkg";
	const std::string longdir = pkg + "/opt/bounce/data/resources/a_folder_with_a_name_long_enough_to_exceed_the_one_hundred_character_tar_limit";
	std::string cmd = "mkdir -p '" + pkg + "/DEBIAN' '" + pkg + "/opt/bounce/data/bundles/original' '" + pkg + "/opt/bounce/data/resources/models' '" +
	                  pkg + "/opt/bounce/data/resources/images' '" + pkg + "/opt/bounce/bin' '" + longdir + "' && " +
	                  "printf 'Package: bounce\\nVersion: 1.0\\nArchitecture: armel\\nMaintainer: x\\nDescription: x\\n' > '" + pkg + "/DEBIAN/control' && " +
	                  "echo level > '" + pkg + "/opt/bounce/data/bundles/original/level01.hgr' && " +
	                  "echo model > '" + pkg + "/opt/bounce/data/resources/models/bounce.hgr' && " +
	                  "echo menu > '" + pkg + "/opt/bounce/data/resources/images/MENU.dat' && " +
	                  "echo long > '" + longdir + "/file_with_an_equally_long_name.pvr' && " +
	                  "echo binary > '" + pkg + "/opt/bounce/bin/bounce' && " +
	                  "dpkg-deb -Zgzip --build '" + pkg + "' '" + work + "/bounce.deb' >/dev/null";
	if (system(cmd.c_str()) != 0) { printf("could not build the test .deb\n"); return 1; }

	printf("sha256 matches the standard test vectors\n");
	CHECK(sha256Hex((const unsigned char*)"abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(sha256Hex((const unsigned char*)"", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	const std::string million(1000000, 'a');
	CHECK(sha256Hex((const unsigned char*)million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

	printf("imports opt/bounce/data from a .deb with data.tar.gz\n");
	int seen = 0;
	DebImportResult r = importDeb(work + "/bounce.deb", work + "/game", [&](const std::string&, int n) { seen = n; });
	CHECK(r.ok);
	if (!r.ok) printf("  error: %s\n", r.error.c_str());
	CHECK(r.files == 4 && seen == 4);
	CHECK(gameDataPresent(work + "/game"));
	CHECK(r.sha256.size() == 64 && !r.knownVersion);
	FILE* f = fopen((work + "/game/data/resources/a_folder_with_a_name_long_enough_to_exceed_the_one_hundred_character_tar_limit/file_with_an_equally_long_name.pvr").c_str(), "r");
	CHECK(f != 0);
	if (f) fclose(f);
	f = fopen((work + "/game/bin/bounce").c_str(), "r");
	CHECK(f == 0);
	if (f) fclose(f);

	printf("rejects non-.deb files and packages without the game\n");
	CHECK(!importDeb(work + "/game/data/bundles/original/level01.hgr", work + "/x").ok);
	system(("rm -rf '" + pkg + "/opt/bounce/data/resources' && dpkg-deb -Zgzip --build '" + pkg + "' '" + work + "/other.deb' >/dev/null").c_str());
	DebImportResult r2 = importDeb(work + "/other.deb", work + "/other");
	CHECK(!r2.ok && !r2.error.empty());

	printf("%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
