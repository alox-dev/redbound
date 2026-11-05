#pragma once
#include <vector>

namespace redbound {

std::vector<int> arcadeRecords();
bool arcadeRecordsAreDefault();
int recordArcadeScore(int meters, std::vector<int>& scores);
void clearArcadeRecords();

}
