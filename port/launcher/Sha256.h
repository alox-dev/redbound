#pragma once
#include <cstddef>
#include <string>

namespace redbound {

std::string sha256Hex(const unsigned char* data, size_t size);

}
