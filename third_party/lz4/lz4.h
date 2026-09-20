// LZ4 by Yann Collet - https://github.com/lz4/lz4
// BSD 2-Clause License - vendored for Calypso
// Minimal header for LZ4 compression/decompression
#ifndef LZ4_H_2983827168210
#define LZ4_H_2983827168210

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define LZ4_VERSION_MAJOR    1
#define LZ4_VERSION_MINOR    9
#define LZ4_VERSION_RELEASE  4

#define LZ4_MAX_INPUT_SIZE   0x7E000000

#define LZ4_COMPRESSBOUND(isize) \
    ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize)/255) + 16)

int LZ4_compressBound(int inputSize);
int LZ4_compress_default(const char* src, char* dst, int srcSize, int dstCapacity);
int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity);

typedef union LZ4_stream_u LZ4_stream_t;

typedef struct {
    const unsigned char* externalDict;
    size_t extDictSize;
    const unsigned char* prefixEnd;
    size_t prefixSize;
} LZ4_streamDecode_t_internal;

typedef union {
    unsigned long long table[4];
    LZ4_streamDecode_t_internal internal_donotuse;
} LZ4_streamDecode_t;

#ifdef __cplusplus
}
#endif

#endif
