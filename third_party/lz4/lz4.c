// LZ4 by Yann Collet - https://github.com/lz4/lz4
// BSD 2-Clause License - vendored for Calypso
//
// Minimal LZ4 block compression/decompression.
// For the full implementation, see the upstream repo.

#include "lz4.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define LZ4_MEMORY_USAGE 14
#define LZ4_HASHLOG (LZ4_MEMORY_USAGE - 2)
#define LZ4_HASHTABLESIZE (1 << LZ4_HASHLOG)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG)

#define MINMATCH 4
#define COPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (COPYLENGTH + MINMATCH)
#define LZ4_minLength (MFLIMIT + 1)
#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)
#define LZ4_DISTANCE_MAX 65535
#define LZ4_SKIP_TRIGGER 6

typedef struct { uint32_t hashTable[LZ4_HASH_SIZE_U32]; } LZ4_stream_t_internal;

union LZ4_stream_u {
    unsigned long long table[LZ4_HASHTABLESIZE >> 1];
    LZ4_stream_t_internal internal_donotuse;
};

int LZ4_compressBound(int inputSize) {
    return (inputSize > LZ4_MAX_INPUT_SIZE) ? 0 : inputSize + (inputSize / 255) + 16;
}

static unsigned int LZ4_hashPosition(const void* p) {
    unsigned int val;
    memcpy(&val, p, 4);
    return (val * 2654435761U) >> (32 - LZ4_HASHLOG);
}

static void LZ4_putPosition(const unsigned char* p, void* tableBase, const unsigned char* srcBase) {
    uint32_t* hashTable = (uint32_t*)tableBase;
    unsigned int h = LZ4_hashPosition(p);
    hashTable[h] = (uint32_t)(p - srcBase);
}

static const unsigned char* LZ4_getPosition(const unsigned char* p, const void* tableBase, const unsigned char* srcBase) {
    const uint32_t* hashTable = (const uint32_t*)tableBase;
    unsigned int h = LZ4_hashPosition(p);
    return srcBase + hashTable[h];
}

static void LZ4_write16(void* p, uint16_t v) { memcpy(p, &v, 2); }
static void LZ4_wildCopy(unsigned char* d, const unsigned char* s, unsigned char* e) {
    do { memcpy(d, s, 8); d += 8; s += 8; } while (d < e);
}

int LZ4_compress_default(const char* source, char* dest, int inputSize, int maxOutputSize) {
    const unsigned char* ip = (const unsigned char*)source;
    const unsigned char* base = ip;
    const unsigned char* anchor = ip;
    const unsigned char* const iend = ip + inputSize;
    const unsigned char* const mflimit = iend - MFLIMIT;
    const unsigned char* const matchlimit = iend - LASTLITERALS;

    unsigned char* op = (unsigned char*)dest;
    unsigned char* const oend = op + maxOutputSize;

    uint32_t hashTable[LZ4_HASH_SIZE_U32];
    memset(hashTable, 0, sizeof(hashTable));

    if (inputSize < LZ4_minLength) goto _last_literals;

    LZ4_putPosition(ip, hashTable, base);
    ip++;
    unsigned int forwardH = LZ4_hashPosition(ip);

    for (;;) {
        const unsigned char* match;
        unsigned char* token;

        { // find a match
            const unsigned char* forwardIp = ip;
            unsigned int step = 1;
            unsigned int searchMatchNb = (1U << LZ4_SKIP_TRIGGER);

            do {
                unsigned int h = forwardH;
                ip = forwardIp;
                forwardIp += step;
                step = (searchMatchNb++ >> LZ4_SKIP_TRIGGER);

                if (forwardIp > mflimit) goto _last_literals;

                match = base + hashTable[h];
                forwardH = LZ4_hashPosition(forwardIp);
                hashTable[h] = (uint32_t)(ip - base);
            } while ((match + LZ4_DISTANCE_MAX < ip) ||
                     (memcmp(match, ip, 4) != 0));
        }

        // Encode literals
        { unsigned int litLength = (unsigned int)(ip - anchor);
          token = op++;
          if ((op + litLength + (litLength >> 8) + 2 + (litLength < 15 ? 0 : 1)) > oend) return 0;
          if (litLength >= RUN_MASK) {
              int len = (int)(litLength - RUN_MASK);
              *token = (unsigned char)(RUN_MASK << ML_BITS);
              for (; len >= 255; len -= 255) *op++ = 255;
              *op++ = (unsigned char)len;
          } else {
              *token = (unsigned char)(litLength << ML_BITS);
          }
          memcpy(op, anchor, litLength);
          op += litLength;
        }

_next_match:
        // Encode offset
        LZ4_write16(op, (uint16_t)(ip - match));
        op += 2;

        // Encode match length
        { unsigned int matchLength = 0;
          while ((ip + matchLength + MINMATCH < matchlimit) &&
                 (ip[matchLength + MINMATCH] == match[matchLength + MINMATCH]))
              matchLength++;

          if (op + (matchLength >> 8) + 1 > oend) return 0;
          if (matchLength >= ML_MASK) {
              *token += (unsigned char)ML_MASK;
              int len = (int)(matchLength - ML_MASK);
              for (; len >= 255; len -= 255) *op++ = 255;
              *op++ = (unsigned char)len;
          } else {
              *token += (unsigned char)matchLength;
          }
          ip += matchLength + MINMATCH;
        }

        anchor = ip;
        if (ip > mflimit) break;

        LZ4_putPosition(ip - 2, hashTable, base);

        match = base + hashTable[LZ4_hashPosition(ip)];
        hashTable[LZ4_hashPosition(ip)] = (uint32_t)(ip - base);
        if ((match + LZ4_DISTANCE_MAX >= ip) && (memcmp(match, ip, 4) == 0)) {
            token = op++;
            *token = 0;
            goto _next_match;
        }

        forwardH = LZ4_hashPosition(++ip);
    }

_last_literals:
    { size_t lastRun = (size_t)(iend - anchor);
      if ((op + lastRun + 1 + ((lastRun + 255 - RUN_MASK) / 255)) > oend) return 0;
      if (lastRun >= RUN_MASK) {
          size_t accumulator = lastRun - RUN_MASK;
          *op++ = (unsigned char)(RUN_MASK << ML_BITS);
          for (; accumulator >= 255; accumulator -= 255) *op++ = 255;
          *op++ = (unsigned char)accumulator;
      } else {
          *op++ = (unsigned char)(lastRun << ML_BITS);
      }
      memcpy(op, anchor, lastRun);
      op += lastRun;
    }

    return (int)(op - (unsigned char*)dest);
}

int LZ4_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize) {
    const unsigned char* ip = (const unsigned char*)source;
    const unsigned char* const iend = ip + compressedSize;

    unsigned char* op = (unsigned char*)dest;
    unsigned char* const oend = op + maxDecompressedSize;
    unsigned char* cpy;

    if (compressedSize == 0) return (maxDecompressedSize == 0) ? 0 : -1;

    while (1) {
        unsigned int token = *ip++;
        unsigned int length;

        // Decode literal length
        length = token >> ML_BITS;
        if (length == RUN_MASK) {
            unsigned int s;
            do {
                if (ip >= iend) return -1;
                s = *ip++;
                length += s;
            } while (s == 255);
        }

        // Copy literals
        cpy = op + length;
        if ((cpy > oend) || (ip + length > iend - (2 + 1 + LASTLITERALS))) {
            if (cpy != oend || ip + length != iend) {
                if (cpy > oend) return -1;
                if (ip + length > iend) return -1;
                memcpy(op, ip, length);
                op += length;
                break;
            }
            memcpy(op, ip, length);
            op += length;
            break;
        }
        memcpy(op, ip, length);
        ip += length;
        op = cpy;

        // Decode offset
        uint16_t offset;
        memcpy(&offset, ip, 2);
        ip += 2;
        const unsigned char* match = op - offset;
        if (match < (const unsigned char*)dest) return -1;

        // Decode match length
        length = token & ML_MASK;
        if (length == ML_MASK) {
            unsigned int s;
            do {
                if (ip >= iend) return -1;
                s = *ip++;
                length += s;
            } while (s == 255);
        }
        length += MINMATCH;

        // Copy match
        cpy = op + length;
        if (cpy > oend) return -1;

        if (offset < 8) {
            for (size_t i = 0; i < length; i++)
                op[i] = match[i];
            op = cpy;
        } else {
            if (length <= 16 && offset >= 8) {
                memcpy(op, match, 16);
                op = cpy;
            } else {
                unsigned char* e = op + length;
                do { memcpy(op, match, 8); match += 8; op += 8; } while (op < e);
                op = cpy;
            }
        }
    }

    return (int)(op - (unsigned char*)dest);
}
