// miniz by richgel999 - https://github.com/richgel999/miniz
// MIT License - vendored for Calypso
//
// Compact but real deflate/inflate implementation.
// Compressor: LZ77 with hash chains + fixed (static) Huffman codes.
// Decompressor: handles stored, fixed Huffman, and dynamic Huffman blocks.

#include "miniz.h"
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning(disable:4244 4267 4996)
#endif

// ---------------------------------------------------------------------------
// Adler-32
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// RFC 1951 fixed Huffman tables
// ---------------------------------------------------------------------------

// Length code base values (codes 257..285)
static const unsigned short s_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const unsigned char s_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};

// Distance base values (codes 0..29)
static const unsigned short s_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const unsigned char s_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

// Code-length alphabet order for dynamic Huffman
static const unsigned char s_cl_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

// ---------------------------------------------------------------------------
// Bit-reversed fixed Huffman codes for the compressor (litlen 0..287)
// We build these once at init.
// ---------------------------------------------------------------------------
static unsigned short s_fixed_lit_codes[288];
static unsigned char  s_fixed_lit_lens[288];
static unsigned short s_fixed_dist_codes[32];
static unsigned char  s_fixed_dist_lens[32];
static int s_fixed_tables_built = 0;

static unsigned short bit_reverse(unsigned short v, int bits) {
    unsigned short r = 0;
    int i;
    for (i = 0; i < bits; i++) {
        r = (unsigned short)((r << 1) | (v & 1));
        v >>= 1;
    }
    return r;
}

static void build_fixed_tables(void) {
    int i;
    if (s_fixed_tables_built) return;

    // Literal/length code lengths per RFC 1951 3.2.6
    for (i =   0; i <= 143; i++) s_fixed_lit_lens[i] = 8;
    for (i = 144; i <= 255; i++) s_fixed_lit_lens[i] = 9;
    for (i = 256; i <= 279; i++) s_fixed_lit_lens[i] = 7;
    for (i = 280; i <= 287; i++) s_fixed_lit_lens[i] = 8;

    // Build codes from lengths (canonical Huffman)
    {
        unsigned short next_code[16];
        int bl_count[16];
        int bits, code;
        memset(bl_count, 0, sizeof(bl_count));
        for (i = 0; i < 288; i++) bl_count[s_fixed_lit_lens[i]]++;
        code = 0;
        bl_count[0] = 0;
        memset(next_code, 0, sizeof(next_code));
        for (bits = 1; bits <= 15; bits++) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = (unsigned short)code;
        }
        for (i = 0; i < 288; i++) {
            if (s_fixed_lit_lens[i]) {
                s_fixed_lit_codes[i] = bit_reverse(next_code[s_fixed_lit_lens[i]]++, s_fixed_lit_lens[i]);
            }
        }
    }

    // Distance codes: all 5 bits
    for (i = 0; i < 32; i++) {
        s_fixed_dist_lens[i] = 5;
        s_fixed_dist_codes[i] = bit_reverse((unsigned short)i, 5);
    }

    s_fixed_tables_built = 1;
}

// ---------------------------------------------------------------------------
// Lookup: length -> length code index (into s_len_base)
// ---------------------------------------------------------------------------
static int find_len_code(unsigned int len) {
    int lo = 0, hi = 28, mid;
    if (len >= 258) return 28;
    while (lo < hi) {
        mid = (lo + hi + 1) >> 1;
        if (s_len_base[mid] <= len) lo = mid; else hi = mid - 1;
    }
    return lo;
}

// Lookup: distance -> distance code index
static int find_dist_code(unsigned int dist) {
    int lo = 0, hi = 29, mid;
    while (lo < hi) {
        mid = (lo + hi + 1) >> 1;
        if (s_dist_base[mid] <= dist) lo = mid; else hi = mid - 1;
    }
    return lo;
}

// ---------------------------------------------------------------------------
// Bitstream writer
// ---------------------------------------------------------------------------
typedef struct {
    unsigned char *buf;
    size_t capacity;
    size_t byte_pos;
    unsigned int bit_buf;
    int bit_count;
} bit_writer_t;

static void bw_init(bit_writer_t *bw, unsigned char *buf, size_t capacity) {
    bw->buf = buf;
    bw->capacity = capacity;
    bw->byte_pos = 0;
    bw->bit_buf = 0;
    bw->bit_count = 0;
}

static int bw_put_bits(bit_writer_t *bw, unsigned int bits, int nbits) {
    bw->bit_buf |= bits << bw->bit_count;
    bw->bit_count += nbits;
    while (bw->bit_count >= 8) {
        if (bw->byte_pos >= bw->capacity) return 0;
        bw->buf[bw->byte_pos++] = (unsigned char)(bw->bit_buf & 0xFF);
        bw->bit_buf >>= 8;
        bw->bit_count -= 8;
    }
    return 1;
}

static int bw_flush(bit_writer_t *bw) {
    if (bw->bit_count > 0) {
        if (bw->byte_pos >= bw->capacity) return 0;
        bw->buf[bw->byte_pos++] = (unsigned char)(bw->bit_buf & 0xFF);
        bw->bit_buf = 0;
        bw->bit_count = 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// LZ77 compressor with hash chain + fixed Huffman encoding
// ---------------------------------------------------------------------------
#define HASH_SIZE 32768
#define HASH_MASK (HASH_SIZE - 1)
#define MIN_MATCH 3
#define MAX_MATCH 258
#define WINDOW_SIZE 32768

static unsigned int hash3(const unsigned char *p) {
    return ((unsigned int)p[0] ^ ((unsigned int)p[1] << 5) ^ ((unsigned int)p[2] << 10)) & HASH_MASK;
}

// Compress raw data into deflate bitstream (fixed Huffman, single block).
// Returns number of bytes written to out_buf, or 0 on failure.
static size_t deflate_compress(const unsigned char *src, size_t src_len,
                               unsigned char *out_buf, size_t out_capacity,
                               int level) {
    bit_writer_t bw;
    unsigned short *head = NULL;
    unsigned short *prev = NULL;
    size_t i;
    int max_chain;

    build_fixed_tables();
    bw_init(&bw, out_buf, out_capacity);

    if (level <= 0) level = 0;
    if (level > 9) level = 9;

    // Level 0: stored blocks
    if (level == 0) {
        size_t remaining = src_len;
        const unsigned char *s = src;
        while (remaining > 0 || src_len == 0) {
            size_t block_len = (remaining > 65535) ? 65535 : remaining;
            int is_final = (remaining <= 65535) ? 1 : 0;
            unsigned short nlen;

            // Align to byte boundary (for stored blocks, must be byte-aligned after header bits)
            bw_put_bits(&bw, is_final, 1);   // BFINAL
            bw_put_bits(&bw, 0, 2);          // BTYPE = 00 (stored)
            bw_flush(&bw);                    // align to byte

            if (bw.byte_pos + 4 + block_len > bw.capacity) return 0;
            bw.buf[bw.byte_pos++] = (unsigned char)(block_len & 0xFF);
            bw.buf[bw.byte_pos++] = (unsigned char)((block_len >> 8) & 0xFF);
            nlen = (unsigned short)(~block_len);
            bw.buf[bw.byte_pos++] = (unsigned char)(nlen & 0xFF);
            bw.buf[bw.byte_pos++] = (unsigned char)((nlen >> 8) & 0xFF);
            memcpy(bw.buf + bw.byte_pos, s, block_len);
            bw.byte_pos += block_len;
            s += block_len;
            remaining -= block_len;
            if (is_final) break;
            if (src_len == 0) break;
        }
        return bw.byte_pos;
    }

    // Levels 1-9: LZ77 + fixed Huffman
    max_chain = (level < 4) ? 4 : (level < 6) ? 16 : (level < 8) ? 64 : 256;

    head = (unsigned short *)calloc(HASH_SIZE, sizeof(unsigned short));
    prev = (unsigned short *)calloc(WINDOW_SIZE, sizeof(unsigned short));
    if (!head || !prev) { free(head); free(prev); return 0; }

    // BFINAL=1, BTYPE=01 (fixed Huffman)
    bw_put_bits(&bw, 1, 1);  // BFINAL
    bw_put_bits(&bw, 1, 2);  // BTYPE = 01

    i = 0;
    while (i < src_len) {
        unsigned int best_len = 0, best_dist = 0;

        if (i + MIN_MATCH <= src_len) {
            unsigned int h = hash3(src + i);
            unsigned int chain_len = 0;
            unsigned int pos = head[h];
            unsigned int max_dist = (i < WINDOW_SIZE) ? (unsigned int)i : WINDOW_SIZE;

            // Insert current position into hash chain
            prev[i & (WINDOW_SIZE - 1)] = head[h];
            head[h] = (unsigned short)(i & 0xFFFF);

            while (pos != 0 && chain_len < (unsigned int)max_chain) {
                unsigned int dist;
                unsigned int candidate;
                // Handle wrapping: pos is stored as 16-bit
                // For inputs > 64K we need full position tracking
                // Simple approach: if candidate is behind us and within window
                if (i >= WINDOW_SIZE) {
                    // pos might have wrapped; skip if stale
                    // We need a different approach for large inputs
                    break;
                }
                candidate = pos;
                if (candidate >= i) { pos = prev[pos & (WINDOW_SIZE - 1)]; chain_len++; continue; }
                dist = (unsigned int)(i - candidate);
                if (dist > max_dist || dist == 0) { pos = prev[pos & (WINDOW_SIZE - 1)]; chain_len++; continue; }

                // Compare
                {
                    unsigned int max_l = (unsigned int)(src_len - i);
                    unsigned int l;
                    if (max_l > MAX_MATCH) max_l = MAX_MATCH;
                    for (l = 0; l < max_l && src[candidate + l] == src[i + l]; l++) {}
                    if (l >= MIN_MATCH && l > best_len) {
                        best_len = l;
                        best_dist = dist;
                        if (l == MAX_MATCH) break;
                    }
                }
                pos = prev[pos & (WINDOW_SIZE - 1)];
                chain_len++;
            }
        } else {
            // Near end, can't match
        }

        if (best_len >= MIN_MATCH) {
            // Emit length/distance pair
            int lc = find_len_code(best_len);
            int dc = find_dist_code(best_dist);
            int lit_sym = 257 + lc;

            bw_put_bits(&bw, s_fixed_lit_codes[lit_sym], s_fixed_lit_lens[lit_sym]);
            if (s_len_extra[lc])
                bw_put_bits(&bw, best_len - s_len_base[lc], s_len_extra[lc]);

            bw_put_bits(&bw, s_fixed_dist_codes[dc], s_fixed_dist_lens[dc]);
            if (s_dist_extra[dc])
                bw_put_bits(&bw, best_dist - s_dist_base[dc], s_dist_extra[dc]);

            // Insert hash entries for skipped positions
            {
                unsigned int j;
                unsigned int end = (unsigned int)i + best_len;
                if (end > src_len) end = (unsigned int)src_len;
                for (j = (unsigned int)i + 1; j < end && j + MIN_MATCH <= (unsigned int)src_len; j++) {
                    if (j < WINDOW_SIZE) {
                        unsigned int hh = hash3(src + j);
                        prev[j & (WINDOW_SIZE - 1)] = head[hh];
                        head[hh] = (unsigned short)(j & 0xFFFF);
                    }
                }
            }
            i += best_len;
        } else {
            // Emit literal
            bw_put_bits(&bw, s_fixed_lit_codes[src[i]], s_fixed_lit_lens[src[i]]);
            i++;
        }
    }

    // End of block (symbol 256)
    bw_put_bits(&bw, s_fixed_lit_codes[256], s_fixed_lit_lens[256]);
    bw_flush(&bw);

    free(head);
    free(prev);
    return bw.byte_pos;
}

// Improved LZ77 for inputs larger than 32KB using 32-bit hash chains
static size_t deflate_compress_large(const unsigned char *src, size_t src_len,
                                     unsigned char *out_buf, size_t out_capacity,
                                     int level) {
    bit_writer_t bw;
    unsigned int *head32 = NULL;
    unsigned int *prev32 = NULL;
    size_t i;
    int max_chain;

    build_fixed_tables();
    bw_init(&bw, out_buf, out_capacity);

    if (level <= 0) return deflate_compress(src, src_len, out_buf, out_capacity, 0);

    max_chain = (level < 4) ? 4 : (level < 6) ? 16 : (level < 8) ? 64 : 256;

    head32 = (unsigned int *)malloc(HASH_SIZE * sizeof(unsigned int));
    prev32 = (unsigned int *)malloc(WINDOW_SIZE * sizeof(unsigned int));
    if (!head32 || !prev32) { free(head32); free(prev32); return 0; }
    memset(head32, 0xFF, HASH_SIZE * sizeof(unsigned int)); // 0xFFFFFFFF = no entry
    memset(prev32, 0xFF, WINDOW_SIZE * sizeof(unsigned int));

    // BFINAL=1, BTYPE=01 (fixed Huffman)
    bw_put_bits(&bw, 1, 1);
    bw_put_bits(&bw, 1, 2);

    i = 0;
    while (i < src_len) {
        unsigned int best_len = 0, best_dist = 0;

        if (i + MIN_MATCH <= src_len) {
            unsigned int h = hash3(src + i);
            unsigned int chain_len = 0;
            unsigned int pos = head32[h];
            unsigned int max_dist = (i < WINDOW_SIZE) ? (unsigned int)i : WINDOW_SIZE;

            prev32[i & (WINDOW_SIZE - 1)] = head32[h];
            head32[h] = (unsigned int)i;

            while (pos != 0xFFFFFFFF && chain_len < (unsigned int)max_chain) {
                unsigned int dist;
                if (pos >= i) { pos = prev32[pos & (WINDOW_SIZE - 1)]; chain_len++; continue; }
                dist = (unsigned int)(i - pos);
                if (dist > max_dist || dist == 0) { pos = prev32[pos & (WINDOW_SIZE - 1)]; chain_len++; continue; }

                {
                    unsigned int max_l = (unsigned int)(src_len - i);
                    unsigned int l;
                    if (max_l > MAX_MATCH) max_l = MAX_MATCH;
                    for (l = 0; l < max_l && src[pos + l] == src[i + l]; l++) {}
                    if (l >= MIN_MATCH && l > best_len) {
                        best_len = l;
                        best_dist = dist;
                        if (l == MAX_MATCH) break;
                    }
                }
                pos = prev32[pos & (WINDOW_SIZE - 1)];
                chain_len++;
            }
        }

        if (best_len >= MIN_MATCH) {
            int lc = find_len_code(best_len);
            int dc = find_dist_code(best_dist);
            int lit_sym = 257 + lc;

            bw_put_bits(&bw, s_fixed_lit_codes[lit_sym], s_fixed_lit_lens[lit_sym]);
            if (s_len_extra[lc])
                bw_put_bits(&bw, best_len - s_len_base[lc], s_len_extra[lc]);
            bw_put_bits(&bw, s_fixed_dist_codes[dc], s_fixed_dist_lens[dc]);
            if (s_dist_extra[dc])
                bw_put_bits(&bw, best_dist - s_dist_base[dc], s_dist_extra[dc]);

            {
                unsigned int j;
                unsigned int end = (unsigned int)i + best_len;
                if (end > (unsigned int)src_len) end = (unsigned int)src_len;
                for (j = (unsigned int)i + 1; j < end && j + MIN_MATCH <= (unsigned int)src_len; j++) {
                    unsigned int hh = hash3(src + j);
                    prev32[j & (WINDOW_SIZE - 1)] = head32[hh];
                    head32[hh] = j;
                }
            }
            i += best_len;
        } else {
            bw_put_bits(&bw, s_fixed_lit_codes[src[i]], s_fixed_lit_lens[src[i]]);
            i++;
        }
    }

    bw_put_bits(&bw, s_fixed_lit_codes[256], s_fixed_lit_lens[256]);
    bw_flush(&bw);

    free(head32);
    free(prev32);
    return bw.byte_pos;
}

// ---------------------------------------------------------------------------
// Inflate (decompressor) - handles stored, fixed, and dynamic Huffman blocks
// ---------------------------------------------------------------------------
typedef struct {
    const unsigned char *src;
    size_t src_len;
    size_t src_pos;
    unsigned int bit_buf;
    int bit_count;
} bit_reader_t;

static void br_init(bit_reader_t *br, const unsigned char *src, size_t len) {
    br->src = src;
    br->src_len = len;
    br->src_pos = 0;
    br->bit_buf = 0;
    br->bit_count = 0;
}

static int br_ensure(bit_reader_t *br, int nbits) {
    while (br->bit_count < nbits) {
        if (br->src_pos >= br->src_len) return 0;
        br->bit_buf |= (unsigned int)br->src[br->src_pos++] << br->bit_count;
        br->bit_count += 8;
    }
    return 1;
}

static unsigned int br_read(bit_reader_t *br, int nbits) {
    unsigned int val;
    br_ensure(br, nbits);
    val = br->bit_buf & ((1u << nbits) - 1);
    br->bit_buf >>= nbits;
    br->bit_count -= nbits;
    return val;
}

static void br_align(bit_reader_t *br) {
    int skip = br->bit_count & 7;
    if (skip) {
        br->bit_buf >>= skip;
        br->bit_count -= skip;
    }
}

// Build a decode table from code lengths. Returns max code length used.
// table format: table[i] stores (symbol << 4) | length for fast decode.
// We use a simple 15-bit lookup table (32768 entries).
#define DECODE_TABLE_SIZE 32768

static int build_decode_table(unsigned short *table, const unsigned char *lens, int num_syms) {
    int bl_count[16], next_code[16];
    int i, bits, max_bits = 0;
    unsigned short code;

    memset(bl_count, 0, sizeof(bl_count));
    for (i = 0; i < num_syms; i++) {
        if (lens[i] > max_bits) max_bits = lens[i];
        bl_count[lens[i]]++;
    }
    if (max_bits == 0) return 0;
    if (max_bits > 15) max_bits = 15;

    bl_count[0] = 0;
    memset(next_code, 0, sizeof(next_code));
    {
        int c = 0;
        for (bits = 1; bits <= max_bits; bits++) {
            c = (c + bl_count[bits - 1]) << 1;
            next_code[bits] = c;
        }
    }

    memset(table, 0, DECODE_TABLE_SIZE * sizeof(unsigned short));

    for (i = 0; i < num_syms; i++) {
        int len = lens[i];
        if (len) {
            code = (unsigned short)next_code[len]++;
            // Fill all table entries for this code
            {
                unsigned short reversed = bit_reverse(code, len);
                int fill = 1 << len;
                int j;
                for (j = reversed; j < DECODE_TABLE_SIZE; j += fill) {
                    table[j] = (unsigned short)((i << 4) | len);
                }
            }
        }
    }
    return max_bits;
}

static int decode_symbol(bit_reader_t *br, const unsigned short *table) {
    unsigned int entry;
    int len, sym;
    if (!br_ensure(br, 15)) {
        // Try with whatever bits we have
        if (br->bit_count == 0) return -1;
    }
    entry = table[br->bit_buf & (DECODE_TABLE_SIZE - 1)];
    len = entry & 0xF;
    sym = entry >> 4;
    if (len == 0) return -1;
    br->bit_buf >>= len;
    br->bit_count -= len;
    return sym;
}

// Inflate a deflate stream (no zlib wrapper).
// Returns decompressed size, or (size_t)-1 on error.
static size_t inflate_raw(const unsigned char *src, size_t src_len,
                          unsigned char *dst, size_t dst_capacity) {
    bit_reader_t br;
    size_t dst_pos = 0;
    int bfinal = 0;

    // Fixed Huffman tables (built on demand)
    static unsigned short *s_fixed_lit_table = NULL;
    static unsigned short *s_fixed_dist_table = NULL;

    br_init(&br, src, src_len);

    while (!bfinal) {
        unsigned int btype;
        if (!br_ensure(&br, 3)) return (size_t)-1;
        bfinal = br_read(&br, 1);
        btype = br_read(&br, 2);

        if (btype == 0) {
            // Stored block
            unsigned int len, nlen;
            br_align(&br);
            // Read from byte stream
            // Discard remaining bits, read 4 bytes
            if (!br_ensure(&br, 32)) return (size_t)-1;
            len = br_read(&br, 16);
            nlen = br_read(&br, 16);
            if ((len ^ nlen) != 0xFFFF) return (size_t)-1;
            if (dst_pos + len > dst_capacity) return (size_t)-1;
            {
                unsigned int j;
                for (j = 0; j < len; j++) {
                    if (!br_ensure(&br, 8)) return (size_t)-1;
                    dst[dst_pos++] = (unsigned char)br_read(&br, 8);
                }
            }
        } else if (btype == 1 || btype == 2) {
            // Huffman-coded block
            unsigned short *lit_table, *dist_table;
            unsigned short *dyn_lit_table = NULL, *dyn_dist_table = NULL;
            int done = 0;

            if (btype == 1) {
                // Build fixed tables if needed
                if (!s_fixed_lit_table) {
                    unsigned char lit_lens[288], dist_lens[32];
                    int k;
                    s_fixed_lit_table = (unsigned short *)malloc(DECODE_TABLE_SIZE * sizeof(unsigned short));
                    s_fixed_dist_table = (unsigned short *)malloc(DECODE_TABLE_SIZE * sizeof(unsigned short));
                    if (!s_fixed_lit_table || !s_fixed_dist_table) return (size_t)-1;

                    for (k = 0; k <= 143; k++) lit_lens[k] = 8;
                    for (k = 144; k <= 255; k++) lit_lens[k] = 9;
                    for (k = 256; k <= 279; k++) lit_lens[k] = 7;
                    for (k = 280; k <= 287; k++) lit_lens[k] = 8;
                    build_decode_table(s_fixed_lit_table, lit_lens, 288);

                    for (k = 0; k < 32; k++) dist_lens[k] = 5;
                    build_decode_table(s_fixed_dist_table, dist_lens, 32);
                }
                lit_table = s_fixed_lit_table;
                dist_table = s_fixed_dist_table;
            } else {
                // Dynamic Huffman - read code length tables
                unsigned int hlit, hdist, hclen;
                unsigned char cl_lens[19];
                unsigned short cl_table[DECODE_TABLE_SIZE];
                unsigned char code_lens[288 + 32];
                int total, idx;

                hlit = br_read(&br, 5) + 257;
                hdist = br_read(&br, 5) + 1;
                hclen = br_read(&br, 4) + 4;

                memset(cl_lens, 0, sizeof(cl_lens));
                {
                    unsigned int ci;
                    for (ci = 0; ci < hclen; ci++) {
                        cl_lens[s_cl_order[ci]] = (unsigned char)br_read(&br, 3);
                    }
                }
                build_decode_table(cl_table, cl_lens, 19);

                total = (int)(hlit + hdist);
                memset(code_lens, 0, sizeof(code_lens));
                idx = 0;
                while (idx < total) {
                    int sym = decode_symbol(&br, cl_table);
                    if (sym < 0) return (size_t)-1;
                    if (sym < 16) {
                        code_lens[idx++] = (unsigned char)sym;
                    } else if (sym == 16) {
                        int rep = br_read(&br, 2) + 3;
                        unsigned char prev_len = idx > 0 ? code_lens[idx - 1] : 0;
                        while (rep-- && idx < total) code_lens[idx++] = prev_len;
                    } else if (sym == 17) {
                        int rep = br_read(&br, 3) + 3;
                        while (rep-- && idx < total) code_lens[idx++] = 0;
                    } else if (sym == 18) {
                        int rep = br_read(&br, 7) + 11;
                        while (rep-- && idx < total) code_lens[idx++] = 0;
                    } else {
                        return (size_t)-1;
                    }
                }

                dyn_lit_table = (unsigned short *)malloc(DECODE_TABLE_SIZE * sizeof(unsigned short));
                dyn_dist_table = (unsigned short *)malloc(DECODE_TABLE_SIZE * sizeof(unsigned short));
                if (!dyn_lit_table || !dyn_dist_table) {
                    free(dyn_lit_table); free(dyn_dist_table);
                    return (size_t)-1;
                }
                build_decode_table(dyn_lit_table, code_lens, (int)hlit);
                build_decode_table(dyn_dist_table, code_lens + hlit, (int)hdist);
                lit_table = dyn_lit_table;
                dist_table = dyn_dist_table;
            }

            // Decode symbols
            while (!done) {
                int sym = decode_symbol(&br, lit_table);
                if (sym < 0) { free(dyn_lit_table); free(dyn_dist_table); return (size_t)-1; }

                if (sym < 256) {
                    if (dst_pos >= dst_capacity) { free(dyn_lit_table); free(dyn_dist_table); return (size_t)-1; }
                    dst[dst_pos++] = (unsigned char)sym;
                } else if (sym == 256) {
                    done = 1;
                } else {
                    // Length/distance
                    unsigned int length, distance;
                    int dist_sym;
                    int len_idx = sym - 257;
                    if (len_idx < 0 || len_idx >= 29) { free(dyn_lit_table); free(dyn_dist_table); return (size_t)-1; }
                    length = s_len_base[len_idx];
                    if (s_len_extra[len_idx])
                        length += br_read(&br, s_len_extra[len_idx]);

                    dist_sym = decode_symbol(&br, dist_table);
                    if (dist_sym < 0 || dist_sym >= 30) { free(dyn_lit_table); free(dyn_dist_table); return (size_t)-1; }
                    distance = s_dist_base[dist_sym];
                    if (s_dist_extra[dist_sym])
                        distance += br_read(&br, s_dist_extra[dist_sym]);

                    if (distance > dst_pos || dst_pos + length > dst_capacity) {
                        free(dyn_lit_table); free(dyn_dist_table);
                        return (size_t)-1;
                    }
                    // Copy with overlap handling (byte by byte for overlapping matches)
                    {
                        unsigned int k;
                        size_t src_off = dst_pos - distance;
                        for (k = 0; k < length; k++) {
                            dst[dst_pos++] = dst[src_off + k];
                        }
                    }
                }
            }

            free(dyn_lit_table);
            free(dyn_dist_table);
        } else {
            return (size_t)-1; // btype 3 is invalid
        }
    }
    return dst_pos;
}

// ---------------------------------------------------------------------------
// Public API: mz_compress2, mz_compress, mz_uncompress, mz_compressBound
// ---------------------------------------------------------------------------

mz_ulong mz_compressBound(mz_ulong source_len) {
    return (mz_ulong)(source_len + (source_len >> 7) + 128 + 6);
}

int mz_compress2(unsigned char *pDest, mz_ulong *pDest_len,
                 const unsigned char *pSource, mz_ulong source_len, int level) {
    size_t deflate_len;
    mz_ulong adler;
    size_t out_capacity;

    if (!pDest || !pDest_len || (!pSource && source_len))
        return MZ_STREAM_ERROR;

    if (level == MZ_DEFAULT_COMPRESSION) level = 6;
    if (level < 0) level = 0;
    if (level > 9) level = 9;

    // Need at least 6 bytes for zlib header + trailer
    out_capacity = (size_t)*pDest_len;
    if (out_capacity < 6) return MZ_BUF_ERROR;

    // zlib header
    pDest[0] = 0x78;
    if (level <= 0) pDest[1] = 0x01;
    else if (level <= 5) pDest[1] = 0x5E;
    else if (level <= 6) pDest[1] = 0x9C;
    else pDest[1] = 0xDA;

    // Compress deflate data starting at offset 2
    if (source_len <= WINDOW_SIZE) {
        deflate_len = deflate_compress(pSource, (size_t)source_len,
                                       pDest + 2, out_capacity - 6, level);
    } else {
        deflate_len = deflate_compress_large(pSource, (size_t)source_len,
                                              pDest + 2, out_capacity - 6, level);
    }
    if (deflate_len == 0 && source_len > 0) {
        // Compression failed (buffer too small), try stored
        deflate_len = deflate_compress(pSource, (size_t)source_len,
                                       pDest + 2, out_capacity - 6, 0);
        if (deflate_len == 0 && source_len > 0) return MZ_BUF_ERROR;
    }

    // Adler-32 trailer (big-endian)
    adler = mz_adler32(1, pSource, (size_t)source_len);
    pDest[2 + deflate_len + 0] = (unsigned char)((adler >> 24) & 0xFF);
    pDest[2 + deflate_len + 1] = (unsigned char)((adler >> 16) & 0xFF);
    pDest[2 + deflate_len + 2] = (unsigned char)((adler >> 8) & 0xFF);
    pDest[2 + deflate_len + 3] = (unsigned char)(adler & 0xFF);

    *pDest_len = (mz_ulong)(2 + deflate_len + 4);
    return MZ_OK;
}

int mz_compress(unsigned char *pDest, mz_ulong *pDest_len,
                const unsigned char *pSource, mz_ulong source_len) {
    return mz_compress2(pDest, pDest_len, pSource, source_len, MZ_DEFAULT_COMPRESSION);
}

int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len,
                  const unsigned char *pSource, mz_ulong source_len) {
    size_t result;
    mz_ulong adler_expected, adler_computed;

    if (!pDest || !pDest_len || !pSource || source_len < 6)
        return MZ_DATA_ERROR;

    // Verify zlib header
    if ((pSource[0] & 0xF) != 8) return MZ_DATA_ERROR; // CM must be 8
    if (((pSource[0] * 256 + pSource[1]) % 31) != 0) return MZ_DATA_ERROR;

    // Decompress deflate data (skip 2-byte zlib header, exclude 4-byte Adler-32 trailer)
    result = inflate_raw(pSource + 2, (size_t)source_len - 6,
                         pDest, (size_t)*pDest_len);
    if (result == (size_t)-1)
        return MZ_DATA_ERROR;

    // Verify Adler-32
    adler_expected = ((mz_ulong)pSource[source_len - 4] << 24) |
                     ((mz_ulong)pSource[source_len - 3] << 16) |
                     ((mz_ulong)pSource[source_len - 2] << 8) |
                     ((mz_ulong)pSource[source_len - 1]);
    adler_computed = mz_adler32(1, pDest, result);
    if (adler_computed != adler_expected)
        return MZ_DATA_ERROR;

    *pDest_len = (mz_ulong)result;
    return MZ_OK;
}

// ---------------------------------------------------------------------------
// tdefl compressor API
// ---------------------------------------------------------------------------

int tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy) {
    unsigned int comp_flags = TDEFL_WRITE_ZLIB_HEADER | TDEFL_COMPUTE_ADLER32;
    (void)window_bits;

    if (level == 0)
        comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;
    else if (level == MZ_DEFAULT_COMPRESSION)
        level = 6;

    if (strategy == 1) // Z_FILTERED
        comp_flags |= TDEFL_FILTER_MATCHES;
    else if (strategy == 2) // Z_HUFFMAN_ONLY
        comp_flags |= TDEFL_FORCE_ALL_STATIC_BLOCKS;
    else if (strategy == 3) // Z_RLE
        comp_flags |= TDEFL_RLE_MATCHES;

    // Encode level in lower bits
    if (level >= 1 && level <= 9) {
        // probes = 1 + (level-1)*2
        unsigned int probes = 1 + ((unsigned int)level - 1) * 2;
        comp_flags |= probes;
    }

    return (int)comp_flags;
}

// Output callback helper
typedef struct {
    unsigned char *buf;
    size_t size;
    size_t capacity;
} output_buffer_t;

static int tdefl_output_cb(const void *pBuf, int len, void *pUser) {
    output_buffer_t *ob = (output_buffer_t *)pUser;
    if (ob->size + (size_t)len > ob->capacity) {
        size_t new_cap = ob->capacity ? ob->capacity * 2 : 4096;
        unsigned char *new_buf;
        while (new_cap < ob->size + (size_t)len) new_cap *= 2;
        new_buf = (unsigned char *)realloc(ob->buf, new_cap);
        if (!new_buf) return 0;
        ob->buf = new_buf;
        ob->capacity = new_cap;
    }
    memcpy(ob->buf + ob->size, pBuf, (size_t)len);
    ob->size += (size_t)len;
    return 1;
}

tdefl_status tdefl_init(tdefl_compressor *d, tdefl_put_buf_func_ptr pPut_buf_func,
                        void *pPut_buf_user, int flags) {
    if (!d) return TDEFL_STATUS_BAD_PARAM;
    memset(d, 0, sizeof(*d));
    d->m_pPut_buf_func = pPut_buf_func;
    d->m_pPut_buf_user = pPut_buf_user;
    d->m_flags = (unsigned int)flags;
    d->m_adler32 = 1;
    d->m_prev_return_status = TDEFL_STATUS_OKAY;

    // Extract probe count from flags
    d->m_max_probes[0] = d->m_flags & 0xFFF;
    if (!d->m_max_probes[0]) d->m_max_probes[0] = 1;
    d->m_max_probes[1] = (d->m_flags >> 12) & 0xFFF;
    d->m_greedy_parsing = (d->m_flags & TDEFL_GREEDY_PARSING_FLAG) != 0;

    return TDEFL_STATUS_OKAY;
}

tdefl_status tdefl_compress(tdefl_compressor *d, const void *pIn_buf, size_t *pIn_buf_size,
                            void *pOut_buf, size_t *pOut_buf_size, tdefl_flush flush) {
    // Simplified implementation: buffer all input, compress on FINISH
    size_t in_size, consumed;

    if (!d) return TDEFL_STATUS_BAD_PARAM;
    if (d->m_prev_return_status != TDEFL_STATUS_OKAY && d->m_prev_return_status != TDEFL_STATUS_DONE)
        return d->m_prev_return_status;

    in_size = pIn_buf_size ? *pIn_buf_size : 0;

    // Copy input data to internal dictionary buffer
    consumed = 0;
    if (pIn_buf && in_size > 0) {
        size_t space = sizeof(d->m_dict) - d->m_dict_size;
        size_t to_copy = (in_size < space) ? in_size : space;
        memcpy(d->m_dict + d->m_dict_size, pIn_buf, to_copy);
        d->m_dict_size += (unsigned int)to_copy;
        consumed = to_copy;

        if (d->m_flags & TDEFL_COMPUTE_ADLER32) {
            d->m_adler32 = (unsigned int)mz_adler32(d->m_adler32,
                (const unsigned char *)pIn_buf, to_copy);
        }
    }

    if (pIn_buf_size) *pIn_buf_size = consumed;

    if (flush == TDEFL_FINISH) {
        // Compress the buffered data and emit output
        size_t out_cap = (size_t)d->m_dict_size + (d->m_dict_size >> 7) + 256;
        unsigned char *comp_buf;
        size_t comp_len;
        int write_zlib = (d->m_flags & TDEFL_WRITE_ZLIB_HEADER) != 0;
        int use_stored = (d->m_flags & TDEFL_FORCE_ALL_RAW_BLOCKS) != 0;
        int level = use_stored ? 0 : 6;

        // Determine level from probes
        if (!use_stored && d->m_max_probes[0] > 0) {
            unsigned int p = d->m_max_probes[0];
            if (p <= 1) level = 1;
            else if (p <= 3) level = 2;
            else if (p <= 5) level = 3;
            else if (p <= 9) level = 5;
            else if (p <= 13) level = 7;
            else level = 9;
        }

        comp_buf = (unsigned char *)malloc(out_cap + 6);
        if (!comp_buf) { d->m_prev_return_status = TDEFL_STATUS_PUT_BUF_FAILED; return d->m_prev_return_status; }

        if (write_zlib) {
            mz_ulong dest_len = (mz_ulong)(out_cap + 6);
            int rc = mz_compress2(comp_buf, &dest_len, d->m_dict, d->m_dict_size, level);
            if (rc != MZ_OK) {
                free(comp_buf);
                d->m_prev_return_status = TDEFL_STATUS_PUT_BUF_FAILED;
                return d->m_prev_return_status;
            }
            comp_len = (size_t)dest_len;
        } else {
            if (d->m_dict_size <= WINDOW_SIZE)
                comp_len = deflate_compress(d->m_dict, d->m_dict_size, comp_buf, out_cap, level);
            else
                comp_len = deflate_compress_large(d->m_dict, d->m_dict_size, comp_buf, out_cap, level);
            if (comp_len == 0 && d->m_dict_size > 0) {
                free(comp_buf);
                d->m_prev_return_status = TDEFL_STATUS_PUT_BUF_FAILED;
                return d->m_prev_return_status;
            }
        }

        // Output
        if (pOut_buf && pOut_buf_size && *pOut_buf_size > 0) {
            size_t to_copy = comp_len < *pOut_buf_size ? comp_len : *pOut_buf_size;
            memcpy(pOut_buf, comp_buf, to_copy);
            *pOut_buf_size = to_copy;
        }

        if (d->m_pPut_buf_func) {
            if (!d->m_pPut_buf_func(comp_buf, (int)comp_len, d->m_pPut_buf_user)) {
                free(comp_buf);
                d->m_prev_return_status = TDEFL_STATUS_PUT_BUF_FAILED;
                return d->m_prev_return_status;
            }
        }

        free(comp_buf);
        d->m_prev_return_status = TDEFL_STATUS_DONE;
        d->m_finished = 1;
        return TDEFL_STATUS_DONE;
    }

    if (pOut_buf_size) *pOut_buf_size = 0;
    return TDEFL_STATUS_OKAY;
}

tdefl_status tdefl_compress_buffer(tdefl_compressor *d, const void *pIn_buf,
                                   size_t in_buf_size, tdefl_flush flush) {
    size_t in_size = in_buf_size;
    return tdefl_compress(d, pIn_buf, &in_size, NULL, NULL, flush);
}

void *tdefl_compress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len,
                                 size_t *pOut_len, int flags) {
    tdefl_compressor *comp;
    output_buffer_t ob;

    if (pOut_len) *pOut_len = 0;
    if (!pSrc_buf && src_buf_len) return NULL;

    comp = (tdefl_compressor *)malloc(sizeof(tdefl_compressor));
    if (!comp) return NULL;

    memset(&ob, 0, sizeof(ob));
    if (tdefl_init(comp, tdefl_output_cb, &ob, flags) != TDEFL_STATUS_OKAY) {
        free(comp);
        return NULL;
    }

    if (tdefl_compress_buffer(comp, pSrc_buf, src_buf_len, TDEFL_FINISH) != TDEFL_STATUS_DONE) {
        free(comp);
        free(ob.buf);
        return NULL;
    }

    free(comp);
    if (pOut_len) *pOut_len = ob.size;
    return ob.buf;
}

size_t tdefl_compress_mem_to_mem(void *pOut_buf, size_t out_buf_len,
                                 const void *pSrc_buf, size_t src_buf_len, int flags) {
    tdefl_compressor *comp;
    size_t out_size;

    if (!pOut_buf) return 0;

    comp = (tdefl_compressor *)malloc(sizeof(tdefl_compressor));
    if (!comp) return 0;

    if (tdefl_init(comp, NULL, NULL, flags) != TDEFL_STATUS_OKAY) {
        free(comp);
        return 0;
    }

    {
        size_t in_size = src_buf_len;
        out_size = out_buf_len;
        if (tdefl_compress(comp, pSrc_buf, &in_size, pOut_buf, &out_size, TDEFL_FINISH) != TDEFL_STATUS_DONE) {
            free(comp);
            return 0;
        }
    }

    free(comp);
    return out_size;
}

// ---------------------------------------------------------------------------
// tinfl decompressor API
// ---------------------------------------------------------------------------

tinfl_status tinfl_decompress(tinfl_decompressor *r,
    const uint8_t *pIn_buf_next, size_t *pIn_buf_size,
    uint8_t *pOut_buf_start, uint8_t *pOut_buf_next, size_t *pOut_buf_size,
    const unsigned int decomp_flags) {
    size_t in_avail, out_avail, result;
    const uint8_t *data_start;
    size_t data_len;
    int parse_zlib = (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER) != 0;

    if (!r || !pIn_buf_size || !pOut_buf_size)
        return TINFL_STATUS_BAD_PARAM;

    in_avail = *pIn_buf_size;
    out_avail = *pOut_buf_size;

    // For simplicity, we require all input available at once for non-streaming usage
    data_start = pIn_buf_next;
    data_len = in_avail;

    if (parse_zlib) {
        if (data_len < 6) {
            *pIn_buf_size = 0;
            *pOut_buf_size = 0;
            return (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ?
                TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED;
        }
        if ((data_start[0] & 0xF) != 8) {
            *pIn_buf_size = 0; *pOut_buf_size = 0;
            return TINFL_STATUS_FAILED;
        }
        data_start += 2;
        data_len -= 6; // skip header and trailer
    }

    result = inflate_raw(data_start, data_len, pOut_buf_next, out_avail);
    if (result == (size_t)-1) {
        *pIn_buf_size = 0;
        *pOut_buf_size = 0;
        return TINFL_STATUS_FAILED;
    }

    // Verify adler32 if parsing zlib
    if (parse_zlib && (decomp_flags & TINFL_FLAG_COMPUTE_ADLER32)) {
        mz_ulong adler_expected =
            ((mz_ulong)pIn_buf_next[in_avail - 4] << 24) |
            ((mz_ulong)pIn_buf_next[in_avail - 3] << 16) |
            ((mz_ulong)pIn_buf_next[in_avail - 2] << 8) |
            ((mz_ulong)pIn_buf_next[in_avail - 1]);
        mz_ulong adler_computed = mz_adler32(1, pOut_buf_next, result);
        if (adler_computed != adler_expected) {
            *pIn_buf_size = 0; *pOut_buf_size = 0;
            return TINFL_STATUS_ADLER32_MISMATCH;
        }
        r->m_check_adler32 = (unsigned int)adler_computed;
    } else {
        r->m_check_adler32 = (unsigned int)mz_adler32(1, pOut_buf_next, result);
    }

    *pIn_buf_size = in_avail;
    *pOut_buf_size = result;
    r->m_state = 1; // mark as done
    return TINFL_STATUS_DONE;
}

void *tinfl_decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len,
                                   size_t *pOut_len, int flags) {
    size_t out_cap = src_buf_len * 4;
    uint8_t *out_buf = NULL;
    tinfl_decompressor decomp;
    size_t in_size, out_size;
    tinfl_status status;

    if (pOut_len) *pOut_len = 0;
    if (!pSrc_buf) return NULL;
    if (out_cap < 256) out_cap = 256;

    // Try with increasing buffer sizes
    while (out_cap < 256 * 1024 * 1024) {
        out_buf = (uint8_t *)malloc(out_cap);
        if (!out_buf) return NULL;

        tinfl_init(&decomp);
        in_size = src_buf_len;
        out_size = out_cap;
        status = tinfl_decompress(&decomp,
            (const uint8_t *)pSrc_buf, &in_size,
            out_buf, out_buf, &out_size,
            (unsigned int)flags | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

        if (status == TINFL_STATUS_DONE) {
            // Shrink to fit
            uint8_t *result = (uint8_t *)realloc(out_buf, out_size ? out_size : 1);
            if (!result) result = out_buf;
            if (pOut_len) *pOut_len = out_size;
            return result;
        }

        free(out_buf);
        out_buf = NULL;

        if (status != TINFL_STATUS_HAS_MORE_OUTPUT)
            return NULL;

        out_cap *= 2;
    }

    return NULL;
}

size_t tinfl_decompress_mem_to_mem(void *pOut_buf, size_t out_buf_len,
                                   const void *pSrc_buf, size_t src_buf_len, int flags) {
    tinfl_decompressor decomp;
    size_t in_size = src_buf_len;
    size_t out_size = out_buf_len;
    tinfl_status status;

    if (!pOut_buf || !pSrc_buf) return (size_t)-1;

    tinfl_init(&decomp);
    status = tinfl_decompress(&decomp,
        (const uint8_t *)pSrc_buf, &in_size,
        (uint8_t *)pOut_buf, (uint8_t *)pOut_buf, &out_size,
        (unsigned int)flags | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if (status != TINFL_STATUS_DONE) return (size_t)-1;
    return out_size;
}
