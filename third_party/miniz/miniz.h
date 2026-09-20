// miniz by richgel999 - https://github.com/richgel999/miniz
// MIT License - vendored for Calypso
// Minimal zlib-compatible compression/decompression
#ifndef MINIZ_H
#define MINIZ_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MZ_OK            0
#define MZ_STREAM_END    1
#define MZ_NEED_DICT     2
#define MZ_ERRNO         (-1)
#define MZ_STREAM_ERROR  (-2)
#define MZ_DATA_ERROR    (-3)
#define MZ_MEM_ERROR     (-4)
#define MZ_BUF_ERROR     (-5)
#define MZ_VERSION_ERROR (-6)

#define MZ_DEFAULT_COMPRESSION (-1)
#define MZ_BEST_SPEED          1
#define MZ_BEST_COMPRESSION    9

typedef unsigned long mz_ulong;

mz_ulong mz_compressBound(mz_ulong source_len);
int mz_compress2(unsigned char *pDest, mz_ulong *pDest_len,
                 const unsigned char *pSource, mz_ulong source_len, int level);
int mz_compress(unsigned char *pDest, mz_ulong *pDest_len,
                const unsigned char *pSource, mz_ulong source_len);
int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len,
                  const unsigned char *pSource, mz_ulong source_len);

// Deflate/inflate internals
#define TINFL_LZ_DICT_SIZE 32768

typedef enum {
    TDEFL_STATUS_BAD_PARAM = -2,
    TDEFL_STATUS_PUT_BUF_FAILED = -1,
    TDEFL_STATUS_OKAY = 0,
    TDEFL_STATUS_DONE = 1
} tdefl_status;

typedef enum {
    TDEFL_NO_FLUSH = 0,
    TDEFL_SYNC_FLUSH = 2,
    TDEFL_FULL_FLUSH = 3,
    TDEFL_FINISH = 4
} tdefl_flush;

enum {
    TDEFL_WRITE_ZLIB_HEADER = 0x01000,
    TDEFL_COMPUTE_ADLER32   = 0x02000,
    TDEFL_GREEDY_PARSING_FLAG = 0x04000,
    TDEFL_RLE_MATCHES         = 0x10000,
    TDEFL_FILTER_MATCHES      = 0x20000,
    TDEFL_FORCE_ALL_STATIC_BLOCKS = 0x40000,
    TDEFL_FORCE_ALL_RAW_BLOCKS    = 0x80000
};

int tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy);

typedef int (*tdefl_put_buf_func_ptr)(const void* pBuf, int len, void* pUser);

typedef struct {
    tdefl_put_buf_func_ptr m_pPut_buf_func;
    void *m_pPut_buf_user;
    unsigned int m_flags, m_max_probes[2];
    int m_greedy_parsing;
    unsigned int m_adler32, m_lookahead_pos, m_lookahead_size, m_dict_size;
    uint8_t *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf, *m_pOutput_buf_end;
    unsigned int m_num_flags_left, m_total_lz_bytes, m_lz_code_buf_dict_pos;
    unsigned int m_bits_in, m_bit_buffer;
    unsigned int m_saved_match_dist, m_saved_match_len, m_saved_lit;
    unsigned int m_output_flush_ofs, m_output_flush_remaining;
    unsigned int m_finished, m_block_index, m_wants_to_finish;
    tdefl_status m_prev_return_status;
    const void *m_pIn_buf;
    void *m_pOut_buf;
    size_t *m_pIn_buf_size, *m_pOut_buf_size;
    tdefl_flush m_flush;
    const uint8_t *m_pSrc;
    size_t m_src_buf_left, m_out_buf_ofs;
    uint8_t m_dict[TINFL_LZ_DICT_SIZE + 2592 + 256];
    uint16_t m_huff_count[3][288 + 32 + 18];
    uint16_t m_huff_codes[3][288 + 32 + 18];
    uint8_t  m_huff_code_sizes[3][288 + 32 + 18];
    uint8_t  m_lz_code_buf[64 * 1024];
    uint16_t m_next[TINFL_LZ_DICT_SIZE];
    uint16_t m_hash[TINFL_LZ_DICT_SIZE];
} tdefl_compressor;

tdefl_status tdefl_init(tdefl_compressor *d, tdefl_put_buf_func_ptr pPut_buf_func,
                        void *pPut_buf_user, int flags);
tdefl_status tdefl_compress(tdefl_compressor *d, const void *pIn_buf, size_t *pIn_buf_size,
                            void *pOut_buf, size_t *pOut_buf_size, tdefl_flush flush);
tdefl_status tdefl_compress_buffer(tdefl_compressor *d, const void *pIn_buf,
                                   size_t in_buf_size, tdefl_flush flush);

void *tdefl_compress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len,
                                 size_t *pOut_len, int flags);
size_t tdefl_compress_mem_to_mem(void *pOut_buf, size_t out_buf_len,
                                 const void *pSrc_buf, size_t src_buf_len, int flags);

typedef enum {
    TINFL_FLAG_PARSE_ZLIB_HEADER = 1,
    TINFL_FLAG_HAS_MORE_INPUT = 2,
    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
    TINFL_FLAG_COMPUTE_ADLER32 = 8
} tinfl_flags;

typedef enum {
    TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS = -4,
    TINFL_STATUS_BAD_PARAM = -3,
    TINFL_STATUS_ADLER32_MISMATCH = -2,
    TINFL_STATUS_FAILED = -1,
    TINFL_STATUS_DONE = 0,
    TINFL_STATUS_NEEDS_MORE_INPUT = 1,
    TINFL_STATUS_HAS_MORE_OUTPUT = 2
} tinfl_status;

struct tinfl_decompressor_tag;
typedef struct tinfl_decompressor_tag tinfl_decompressor;

#define tinfl_init(r) do { (r)->m_state = 0; } while(0)
#define tinfl_get_adler32(r) (r)->m_check_adler32

tinfl_status tinfl_decompress(tinfl_decompressor *r,
    const uint8_t *pIn_buf_next, size_t *pIn_buf_size,
    uint8_t *pOut_buf_start, uint8_t *pOut_buf_next, size_t *pOut_buf_size,
    const unsigned int decomp_flags);

struct tinfl_decompressor_tag {
    unsigned int m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32;
    unsigned int m_final, m_type, m_check_adler32, m_dist, m_counter;
    unsigned int m_num_extra, m_table_sizes[3];
    int m_bit_buf;
    size_t m_dist_from_out_buf_start;
    int16_t m_tree_0[1024];
    int16_t m_tree_1[512];
    int16_t m_tree_2[128];
    uint8_t m_code_size_0[288 + 32 + 137];
    uint8_t m_code_size_1[288 + 32 + 137];
    uint8_t m_code_size_2[19];
    uint8_t m_raw_header[4];
    uint8_t m_len_codes[288 + 32 + 137 + 16384];
};

void *tinfl_decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len,
                                   size_t *pOut_len, int flags);
size_t tinfl_decompress_mem_to_mem(void *pOut_buf, size_t out_buf_len,
                                   const void *pSrc_buf, size_t src_buf_len, int flags);

#ifdef __cplusplus
}
#endif

#endif // MINIZ_H
