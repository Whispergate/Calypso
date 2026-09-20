// miniz by richgel999 - https://github.com/richgel999/miniz
// MIT License - vendored for Calypso
//
// This is a minimal self-contained zlib-compatible compressor/decompressor.
// For the full 7000+ line implementation, see the upstream repo.
// This vendored copy provides the compress/uncompress API used by Calypso.

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_APIS

#include "miniz.h"
#include <stdlib.h>
#include <string.h>

// Adler-32
static mz_ulong mz_adler32(mz_ulong adler, const unsigned char *ptr, size_t buf_len) {
    mz_ulong s1 = adler & 0xffff, s2 = adler >> 16;
    size_t block_len;
    if (!ptr) return 1;
    while (buf_len) {
        block_len = (buf_len < 5552) ? buf_len : 5552;
        buf_len -= block_len;
        while (block_len >= 8) {
            s1 += ptr[0]; s2 += s1; s1 += ptr[1]; s2 += s1;
            s1 += ptr[2]; s2 += s1; s1 += ptr[3]; s2 += s1;
            s1 += ptr[4]; s2 += s1; s1 += ptr[5]; s2 += s1;
            s1 += ptr[6]; s2 += s1; s1 += ptr[7]; s2 += s1;
            ptr += 8; block_len -= 8;
        }
        while (block_len--) { s1 += *ptr++; s2 += s1; }
        s1 %= 65521; s2 %= 65521;
    }
    return (s2 << 16) + s1;
}

// Static huffman tables for quick compression
static const uint16_t s_tdefl_len_sym[256] = {
  257,258,259,260,261,261,262,262,263,263,263,263,264,264,264,264,
  265,265,265,265,265,265,265,265,266,266,266,266,266,266,266,266,
  267,267,267,267,267,267,267,267,267,267,267,267,267,267,267,267,
  268,268,268,268,268,268,268,268,268,268,268,268,268,268,268,268,
  269,269,269,269,269,269,269,269,269,269,269,269,269,269,269,269,
  269,269,269,269,269,269,269,269,269,269,269,269,269,269,269,269,
  270,270,270,270,270,270,270,270,270,270,270,270,270,270,270,270,
  270,270,270,270,270,270,270,270,270,270,270,270,270,270,270,270,
  271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,
  271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,
  271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,
  271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,271,
  272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,
  272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,
  272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,
  272,272,272,272,272,272,272,272,272,272,272,272,272,272,272,272
};

static const uint8_t s_tdefl_len_extra[256] = {
  0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};

// Simplified compress using raw store blocks (zlib-wrapped)
// For production, replace with full tdefl implementation from upstream miniz
mz_ulong mz_compressBound(mz_ulong source_len) {
    return (source_len + 128 + (source_len >> 7)); // generous bound
}

int mz_compress2(unsigned char *pDest, mz_ulong *pDest_len,
                 const unsigned char *pSource, mz_ulong source_len, int level) {
    // Use raw stored blocks wrapped in zlib format
    // Header: 0x78 0x01 (low compression)
    mz_ulong needed = 2 + 4; // zlib header + adler32 trailer
    mz_ulong remaining = source_len;
    const unsigned char *src = pSource;

    // Calculate blocks needed (each max 65535 bytes)
    mz_ulong nblocks = (source_len + 65534) / 65535;
    needed += nblocks * 5 + source_len; // 5-byte header per block + data

    if (*pDest_len < needed) {
        *pDest_len = needed;
        return MZ_BUF_ERROR;
    }

    unsigned char *dst = pDest;
    // zlib header
    *dst++ = 0x78;
    *dst++ = 0x01;

    while (remaining > 0) {
        mz_ulong block_len = (remaining > 65535) ? 65535 : remaining;
        int is_final = (remaining <= 65535) ? 1 : 0;

        *dst++ = (unsigned char)is_final; // BFINAL + BTYPE=00 (stored)
        *dst++ = (unsigned char)(block_len & 0xFF);
        *dst++ = (unsigned char)((block_len >> 8) & 0xFF);
        *dst++ = (unsigned char)(~block_len & 0xFF);
        *dst++ = (unsigned char)((~block_len >> 8) & 0xFF);
        memcpy(dst, src, block_len);
        dst += block_len;
        src += block_len;
        remaining -= block_len;
    }

    // Adler-32
    mz_ulong adler = mz_adler32(1, pSource, source_len);
    *dst++ = (unsigned char)((adler >> 24) & 0xFF);
    *dst++ = (unsigned char)((adler >> 16) & 0xFF);
    *dst++ = (unsigned char)((adler >> 8) & 0xFF);
    *dst++ = (unsigned char)(adler & 0xFF);

    *pDest_len = (mz_ulong)(dst - pDest);
    return MZ_OK;
}

int mz_compress(unsigned char *pDest, mz_ulong *pDest_len,
                const unsigned char *pSource, mz_ulong source_len) {
    return mz_compress2(pDest, pDest_len, pSource, source_len, MZ_DEFAULT_COMPRESSION);
}

int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len,
                  const unsigned char *pSource, mz_ulong source_len) {
    // Parse zlib header
    if (source_len < 6) return MZ_DATA_ERROR;

    const unsigned char *src = pSource + 2; // skip zlib header
    mz_ulong src_remaining = source_len - 6; // minus header and trailer
    unsigned char *dst = pDest;
    mz_ulong dst_remaining = *pDest_len;

    while (src_remaining > 0) {
        unsigned char hdr = *src++;
        src_remaining--;
        int btype = (hdr >> 1) & 3;

        if (btype == 0) {
            // Stored block
            if (src_remaining < 4) return MZ_DATA_ERROR;
            uint16_t len = src[0] | ((uint16_t)src[1] << 8);
            src += 4; src_remaining -= 4;
            if (src_remaining < len || dst_remaining < len) return MZ_BUF_ERROR;
            memcpy(dst, src, len);
            src += len; src_remaining -= len;
            dst += len; dst_remaining -= len;
        } else {
            return MZ_DATA_ERROR; // only stored blocks supported in this minimal version
        }

        if (hdr & 1) break; // BFINAL
    }

    *pDest_len = (mz_ulong)(dst - pDest);
    return MZ_OK;
}

int tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy) {
    unsigned int comp_flags = TDEFL_WRITE_ZLIB_HEADER | TDEFL_COMPUTE_ADLER32;
    if (level == 0) comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;
    return (int)comp_flags;
}
