#pragma once
#include "config.hpp"
#include <vector>
#include <cstdint>

namespace calypso {

std::vector<uint8_t> compress_zlib(const std::vector<uint8_t>& data);
std::vector<uint8_t> compress_lz4(const std::vector<uint8_t>& data);
std::vector<uint8_t> compress_rle(const std::vector<uint8_t>& data);
std::vector<uint8_t> compress_lznt(const std::vector<uint8_t>& data);

std::vector<uint8_t> compress_payload(const std::vector<uint8_t>& data,
                                       CompressionMethod method);

} // namespace calypso
