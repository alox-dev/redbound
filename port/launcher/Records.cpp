#include "Records.h"
#include "Paths.h"
#include <cstdio>
#include <string>

namespace redbound {

static const std::vector<int> kDefaultRecords = { 3500, 2000, 1000 };

static std::string recordsPath() { return userDataDir() + "/level2.scores"; }

std::vector<int> arcadeRecords() {
	std::vector<int> scores;
	if (FILE* f = fopen(recordsPath().c_str(), "r")) {
		int v;
		while (scores.size() < 3 && fscanf(f, "%d", &v) == 1) scores.push_back(v);
		fclose(f);
	}
	return scores.empty() ? kDefaultRecords : scores;
}

bool arcadeRecordsAreDefault() { return arcadeRecords() == kDefaultRecords; }

int recordArcadeScore(int meters, std::vector<int>& scores) {
	scores = arcadeRecords();
	int rank = -1;
	for (int i = 0; i < (int)scores.size(); ++i)
		if (meters > scores[i]) {
			rank = i;
			break;
		}
	if (rank < 0 && scores.size() < 3) rank = (int)scores.size();
	if (rank >= 0) {
		scores.insert(scores.begin() + rank, meters);
		if (scores.size() > 3) scores.resize(3);
	}
	makeDirectories(userDataDir());
	if (FILE* f = fopen(recordsPath().c_str(), "w")) {
		for (int v : scores) fprintf(f, "%d\n", v);
		fclose(f);
	}
	return rank;
}

void clearArcadeRecords() { remove(recordsPath().c_str()); }

}
