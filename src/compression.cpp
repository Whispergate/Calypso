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

static int lznt_disp_bits(int pos) {
    if (pos < 0x10)  return 12;
    if (pos < 0x20)  return 11;
    if (pos < 0x40)  return 10;
    if (pos < 0x80)  return 9;
    if (pos < 0x100) return 8;
    if (pos < 0x200) return 7;
    if (pos < 0x400) return 6;
    if (pos < 0x800) return 5;
    return 4;
}

static std::vector<uint8_t> lznt_compress_chunk(const uint8_t* src, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len);

    size_t pos = 0;
    while (pos < len) {
        uint8_t flags = 0;
        size_t flag_pos = out.size();
        out.push_back(0);

        for (int bit = 0; bit < 8 && pos < len; bit++) {
            if (pos == 0) {
                out.push_back(src[pos++]);
                continue;
            }

            int db = lznt_disp_bits(static_cast<int>(pos));
            int max_disp = 1 << db;
            int lb = 16 - db;
            int max_len = (1 << lb) + 2;

            int best_len = 0, best_off = 0;
            int search_start = (pos > static_cast<size_t>(max_disp)) ? static_cast<int>(pos) - max_disp : 0;

            for (int s = search_start; s < static_cast<int>(pos); s++) {
                int ml = 0;
                while (pos + ml < len && src[s + ml] == src[pos + ml] && ml < max_len)
                    ml++;
                if (ml > best_len) {
                    best_len = ml;
                    best_off = static_cast<int>(pos) - s;
                }
            }

            if (best_len >= 3) {
                int disp_val = best_off - 1;
                int len_val = best_len - 3;
                uint16_t ref = static_cast<uint16_t>((len_val << db) | disp_val);
                out.push_back(ref & 0xFF);
                out.push_back(ref >> 8);
                flags |= (1 << bit);
                pos += best_len;
            } else {
                out.push_back(src[pos++]);
            }
        }
        out[flag_pos] = flags;
    }
    return out;
}

std::vector<uint8_t> compress_lznt(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    size_t offset = 0;

    while (offset < data.size()) {
        size_t chunk_len = std::min<size_t>(4096, data.size() - offset);
        const uint8_t* chunk = data.data() + offset;

        auto compressed = lznt_compress_chunk(chunk, chunk_len);

        if (compressed.size() < chunk_len) {
            uint16_t header = static_cast<uint16_t>(compressed.size() - 1) | 0xB000;
            out.push_back(header & 0xFF);
            out.push_back(header >> 8);
            out.insert(out.end(), compressed.begin(), compressed.end());
        } else {
            uint16_t header = static_cast<uint16_t>(chunk_len - 1) | 0x3000;
            out.push_back(header & 0xFF);
            out.push_back(header >> 8);
            out.insert(out.end(), chunk, chunk + chunk_len);
        }
        offset += chunk_len;
    }
    // End marker
    out.push_back(0);
    out.push_back(0);
    return out;
}

std::vector<uint8_t> compress_rle(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    out.reserve(data.size());
    size_t i = 0;
    while (i < data.size()) {
        uint8_t val = data[i];
        size_t run = 1;
        while (i + run < data.size() && data[i + run] == val && run < 255)
            run++;
        if (run >= 3 || val == 0xFF) {
            out.push_back(0xFF);
            out.push_back(static_cast<uint8_t>(run));
            out.push_back(val);
        } else {
            for (size_t r = 0; r < run; r++)
                out.push_back(val);
        }
        i += run;
    }
    return out;
}

std::vector<uint8_t> compress_payload(const std::vector<uint8_t>& data,
                                       CompressionMethod method) {
    switch (method) {
        case CompressionMethod::Zlib: return compress_zlib(data);
        case CompressionMethod::LZ4:  return compress_lz4(data);
        case CompressionMethod::RLE:  return compress_rle(data);
        case CompressionMethod::LZNT: return compress_lznt(data);
        case CompressionMethod::None: return data;
    }
    return data;
}

} // namespace calypso
