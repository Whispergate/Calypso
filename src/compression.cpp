#include "calypso/compression.hpp"
#include <iostream>
#include <cstdlib>

extern "C" {
#include "miniz.h"
#include "lz4.h"
}

namespace calypso {

std::vector<uint8_t> compress_zlib(const std::vector<uint8_t>& data) {
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
    std::vector<uint8_t> compressed(bound);
    mz_ulong compressed_size = bound;

    int ret = mz_compress2(compressed.data(), &compressed_size,
                           data.data(), static_cast<mz_ulong>(data.size()),
                           MZ_BEST_COMPRESSION);
    if (ret != MZ_OK) {
        std::cerr << "[!] Zlib compression failed (error " << ret << ")\n";
        std::exit(1);
    }

    compressed.resize(compressed_size);
    return compressed;
}

std::vector<uint8_t> compress_lz4(const std::vector<uint8_t>& data) {
    int bound = LZ4_compressBound(static_cast<int>(data.size()));
    if (bound == 0) {
        std::cerr << "[!] LZ4: input too large\n";
        std::exit(1);
    }

    std::vector<uint8_t> compressed(static_cast<size_t>(bound));
    int compressed_size = LZ4_compress_default(
        reinterpret_cast<const char*>(data.data()),
        reinterpret_cast<char*>(compressed.data()),
        static_cast<int>(data.size()),
        bound);

    if (compressed_size <= 0) {
        std::cerr << "[!] LZ4 compression failed\n";
        std::exit(1);
    }

    compressed.resize(static_cast<size_t>(compressed_size));
    return compressed;
}

std::vector<uint8_t> compress_payload(const std::vector<uint8_t>& data,
                                       CompressionMethod method) {
    switch (method) {
        case CompressionMethod::Zlib: return compress_zlib(data);
        case CompressionMethod::LZ4:  return compress_lz4(data);
        case CompressionMethod::None: return data;
    }
    return data;
}

} // namespace calypso
