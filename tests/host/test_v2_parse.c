// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_v2_parse.c.
 *
 * Buffers are hand-built byte by byte (put_le32) so the tests do not depend on
 * the writer whose round trip they complement.
 */

#include "twlhd_v2_parse.h"

#include "tests_host.h"

static uint8_t g_buf[TWLHD_V2_FILE_MAX + 64u];
static uint8_t g_gx[TWLHD_V2_GXCM_MAX_ENTRIES * TWLHD_V2_GXCM_ENTRY_SIZE + 32u];
static uint8_t g_rs[TWLHD_V2_RSTA_MAX_ENTRIES * TWLHD_V2_RSTA_ENTRY_SIZE + 32u];
static uint8_t g_txds[TWLHD_V2_TXDS_MAX_RECORDS * TWLHD_V2_TXDS_RECORD_SIZE + 64u];
static uint8_t g_txdt[TWLHD_V2_TXDT_MAX_BYTES + 64u];
static uint8_t g_pldt[TWLHD_V2_PLDT_MAX_BYTES + 64u];

static size_t g_gxcm_hdr;
static size_t g_meta_hdr;
static size_t g_rsta_hdr;
static size_t g_txds_hdr;
static size_t g_txdt_hdr;
static size_t g_pldt_hdr;

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void put_meta(uint8_t *p, uint32_t backend, uint32_t coverage,
                     uint32_t reserved0, uint32_t reserved1) {
    put_le32(p + 0, backend);
    put_le32(p + 4, coverage);
    put_le32(p + 8, reserved0);
    put_le32(p + 12, reserved1);
}

static size_t emit_chunk_header(size_t offset, uint32_t type,
                                uint32_t payload_size, uint32_t chunk_flags) {
    put_le32(g_buf + offset + 0, type);
    put_le32(g_buf + offset + 4, payload_size);
    put_le32(g_buf + offset + 8, chunk_flags);
    put_le32(g_buf + offset + 12, 0);
    return offset + TWLHD_V2_CHUNK_HEADER_SIZE;
}

static void put_gxcm_entry(uint32_t index, uint32_t cmd_word, uint32_t value) {
    put_le32(g_gx + (size_t)index * 8u + 0, cmd_word);
    put_le32(g_gx + (size_t)index * 8u + 4, value);
}

static void put_rsta_entry(uint32_t index, uint32_t addr, uint32_t value,
                           uint32_t gx_index, uint32_t width) {
    uint8_t *p = g_rs + (size_t)index * 16u;
    put_le32(p + 0, addr);
    put_le32(p + 4, value);
    put_le32(p + 8, gx_index);
    put_le32(p + 12, width);
}

static void put_tex_record(uint32_t index, uint32_t param, uint32_t pltt,
                           uint32_t texel_off, uint32_t texel_size,
                           uint32_t palette_off, uint32_t palette_size,
                           uint32_t index_off, uint32_t index_size) {
    uint8_t *p = g_txds + (size_t)index * TWLHD_V2_TXDS_RECORD_SIZE;

    put_le32(p + 0, param);
    put_le32(p + 4, pltt);
    put_le32(p + 8, texel_off);
    put_le32(p + 12, texel_size);
    put_le32(p + 16, palette_off);
    put_le32(p + 20, palette_size);
    put_le32(p + 24, index_off);
    put_le32(p + 28, index_size);
    put_le32(p + 32, 0);
    put_le32(p + 36, 0);
}

/*
 * Build GXCM + META, then any of TXDS/TXDT/PLDT whose size is nonzero, in the
 * canonical chunk order. g_gx must be filled by the caller.
 */
static size_t build_tex(uint32_t gx_size, const uint8_t *meta,
                        uint32_t meta_size, const uint8_t *txds,
                        uint32_t txds_size, const uint8_t *txdt,
                        uint32_t txdt_size, const uint8_t *pldt,
                        uint32_t pldt_size, uint32_t flags,
                        uint32_t frame_index) {
    size_t offset = TWLHD_V2_HEADER_SIZE;
    uint32_t chunk_count = 2u;

    g_gxcm_hdr = offset;
    offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_GXCM, gx_size,
                               TWLHD_V2_CHUNK_FLAG_REQUIRED);
    if (gx_size != 0u) memcpy(g_buf + offset, g_gx, gx_size);
    offset += gx_size;

    g_meta_hdr = offset;
    offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_META, meta_size, 0);
    if (meta_size != 0u) memcpy(g_buf + offset, meta, meta_size);
    offset += meta_size;

    g_txds_hdr = 0;
    if (txds_size != 0u) {
        chunk_count++;
        g_txds_hdr = offset;
        offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_TXDS, txds_size, 0);
        memcpy(g_buf + offset, txds, txds_size);
        offset += txds_size;
    }

    g_txdt_hdr = 0;
    if (txdt_size != 0u) {
        chunk_count++;
        g_txdt_hdr = offset;
        offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_TXDT, txdt_size, 0);
        memcpy(g_buf + offset, txdt, txdt_size);
        offset += txdt_size;
    }

    g_pldt_hdr = 0;
    if (pldt_size != 0u) {
        chunk_count++;
        g_pldt_hdr = offset;
        offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_PLDT, pldt_size, 0);
        memcpy(g_buf + offset, pldt, pldt_size);
        offset += pldt_size;
    }

    put_le32(g_buf + 0, TWLHD_V2_VERSION);
    put_le32(g_buf + 4, TWLHD_V2_HEADER_SIZE);
    put_le32(g_buf + 8, (uint32_t)offset);
    put_le32(g_buf + 12, flags);
    put_le32(g_buf + 16, frame_index);
    put_le32(g_buf + 20, chunk_count);
    put_le32(g_buf + 24, 0);
    put_le32(g_buf + 28, 0);
    return offset;
}

/*
 * Build GXCM + META (+ RSTA when rsta_size != 0) with a correct header.
 * Returns the total size.
 */
static size_t build_raw(const uint8_t *gx, uint32_t gx_size,
                        const uint8_t *meta, uint32_t meta_size,
                        const uint8_t *rsta, uint32_t rsta_size,
                        uint32_t flags, uint32_t frame_index) {
    size_t offset = TWLHD_V2_HEADER_SIZE;
    uint32_t chunk_count = 2u;

    g_gxcm_hdr = offset;
    offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_GXCM, gx_size,
                               TWLHD_V2_CHUNK_FLAG_REQUIRED);
    if (gx_size != 0u) memcpy(g_buf + offset, gx, gx_size);
    offset += gx_size;

    g_meta_hdr = offset;
    offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_META, meta_size, 0);
    if (meta_size != 0u) memcpy(g_buf + offset, meta, meta_size);
    offset += meta_size;

    g_rsta_hdr = 0;
    if (rsta_size != 0u) {
        chunk_count = 3u;
        g_rsta_hdr = offset;
        offset = emit_chunk_header(offset, TWLHD_V2_CHUNK_RSTA, rsta_size, 0);
        memcpy(g_buf + offset, rsta, rsta_size);
        offset += rsta_size;
    }

    put_le32(g_buf + 0, TWLHD_V2_VERSION);
    put_le32(g_buf + 4, TWLHD_V2_HEADER_SIZE);
    put_le32(g_buf + 8, (uint32_t)offset);
    put_le32(g_buf + 12, flags);
    put_le32(g_buf + 16, frame_index);
    put_le32(g_buf + 20, chunk_count);
    put_le32(g_buf + 24, 0);
    put_le32(g_buf + 28, 0);
    return offset;
}

/* The golden vector also mirrored in docs/TWLHD_V2_FORMAT.md. */
static size_t build_golden(uint32_t flags) {
    static const uint32_t entries[8][2] = {
        {0x10u, 2u}, {0x15u, 0u}, {0x40u, 1u}, {0x20u, 0x1Fu},
        {0x23u, 0x1000u}, {0x23u, 0x2000u}, {0x41u, 0u}, {0x50u, 0u},
    };
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    uint32_t i;

    for (i = 0; i < 8u; i++) put_gxcm_entry(i, entries[i][0], entries[i][1]);
    put_meta(meta, TWLHD_V2_BACKEND_REGISTER_PROXY,
             TWLHD_V2_COVERAGE_LIBRARY_INTERNAL_WRITES |
                 TWLHD_V2_COVERAGE_DMA_DISPLAY_LISTS |
                 TWLHD_V2_COVERAGE_INITIAL_STATE_UNKNOWN,
             0, 0);
    put_rsta_entry(0, 0x04000350u, 0x001F3FFFu, 3u, 4u);

    return build_raw(g_gx, 8u * 8u, meta, sizeof meta, g_rs, 16u, flags, 1u);
}

static void test_null_version_truncated(void) {
    TwlhdV2View view;
    size_t size;
    uint32_t bad_sizes[5] = {0u, 1u, 3u, 4u, 31u};
    uint32_t i;

    size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);

    CHECK_EQ_INT(twlhd_v2_parse(NULL, size, &view), TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, NULL), TWLHD_V2_ERR_NULL);

    put_le32(g_buf + 0, 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_VERSION);
    put_le32(g_buf + 0, 3u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_VERSION);
    put_le32(g_buf + 0, TWLHD_V2_VERSION);

    for (i = 0; i < 5u; i++) {
        CHECK_EQ_INT(twlhd_v2_parse(g_buf, bad_sizes[i], &view),
                     TWLHD_V2_ERR_TRUNCATED);
    }
}

static void test_header(void) {
    TwlhdV2View view;
    size_t size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);

    put_le32(g_buf + 4, 33u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_HEADER);
    put_le32(g_buf + 4, TWLHD_V2_HEADER_SIZE);

    put_le32(g_buf + 24, 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_HEADER);
    put_le32(g_buf + 24, 0u);
    put_le32(g_buf + 28, 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_HEADER);
    put_le32(g_buf + 28, 0u);

    put_le32(g_buf + 12, 4u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_FLAGS);
    put_le32(g_buf + 12, 0x80000000u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_FLAGS);
    put_le32(g_buf + 12, TWLHD_V2_FLAG_FRAME_COMPLETE);

    put_le32(g_buf + 20, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_CHUNK_COUNT);
    put_le32(g_buf + 20, 17u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_CHUNK_COUNT);
    put_le32(g_buf + 20, 3u);

    put_le32(g_buf + 8, (uint32_t)size - 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_SIZE);
    put_le32(g_buf + 8, TWLHD_V2_FILE_MAX + 4u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_SIZE);
    put_le32(g_buf + 8, (uint32_t)size);

    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
}

static void test_chunk_structure(void) {
    TwlhdV2View view;
    size_t size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);

    put_le32(g_buf + g_gxcm_hdr + 4, 0xFFFFFFFFu);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_BOUNDS);
    put_le32(g_buf + g_gxcm_hdr + 4, 64u);

    put_le32(g_buf + g_gxcm_hdr + 4, 62u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_ALIGN);
    put_le32(g_buf + g_gxcm_hdr + 4, 64u);

    put_le32(g_buf + g_gxcm_hdr + 8, 2u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_gxcm_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);

    put_le32(g_buf + g_gxcm_hdr + 12, 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_gxcm_hdr + 12, 0u);

    /* Declared chunk count larger than the bytes actually present. */
    put_le32(g_buf + 20, 4u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_BOUNDS);
    put_le32(g_buf + 20, 3u);

    /* Trailing bytes after the last chunk. */
    g_buf[size + 0] = 0xAA;
    g_buf[size + 1] = 0xBB;
    g_buf[size + 2] = 0xCC;
    g_buf[size + 3] = 0xDD;
    put_le32(g_buf + 8, (uint32_t)size + 4u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size + 4u, &view),
                 TWLHD_V2_ERR_CHUNK_BOUNDS);
}

static void test_chunks(void) {
    TwlhdV2View view;
    size_t size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);

    /* Duplicate known chunk. */
    put_le32(g_buf + g_meta_hdr + 0, TWLHD_V2_CHUNK_GXCM);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_DUP_CHUNK);
    put_le32(g_buf + g_meta_hdr + 0, TWLHD_V2_CHUNK_META);

    /* Unknown optional chunk is skipped and counted. */
    put_le32(g_buf + g_rsta_hdr + 0, TWLHD_FOURCC('Z', 'Z', 'Z', 'Z'));
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.unknown_chunks_skipped, 1);
    CHECK_EQ_INT(view.rsta_count, 0);
    CHECK(view.rsta == NULL);

    /* Unknown REQUIRED chunk is a hard failure. */
    put_le32(g_buf + g_rsta_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_UNSUPPORTED_REQUIRED);
    put_le32(g_buf + g_rsta_hdr + 8, 0u);

    /* CLPM is still reserved: unknown + optional. */
    put_le32(g_buf + g_rsta_hdr + 0, TWLHD_V2_CHUNK_CLPM);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    /*
     * TXDS is a known chunk now, so the 16-byte RSTA payload reused as its
     * payload is rejected as a bad record size instead of being skipped.
     */
    put_le32(g_buf + g_rsta_hdr + 0, TWLHD_V2_CHUNK_TXDS);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS);
    put_le32(g_buf + g_rsta_hdr + 0, TWLHD_V2_CHUNK_RSTA);

    /* Missing known chunks. */
    put_le32(g_buf + g_gxcm_hdr + 0, TWLHD_FOURCC('Q', 'Q', 'Q', 'Q'));
    put_le32(g_buf + g_gxcm_hdr + 8, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_MISSING_CHUNK);
    put_le32(g_buf + g_gxcm_hdr + 0, TWLHD_V2_CHUNK_GXCM);
    put_le32(g_buf + g_gxcm_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);

    put_le32(g_buf + g_meta_hdr + 0, TWLHD_FOURCC('Q', 'Q', 'Q', 'Q'));
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_MISSING_CHUNK);
    put_le32(g_buf + g_meta_hdr + 0, TWLHD_V2_CHUNK_META);

    /* REQUIRED-bit expectations for known chunks. */
    put_le32(g_buf + g_gxcm_hdr + 8, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_gxcm_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);

    put_le32(g_buf + g_meta_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_meta_hdr + 8, 0u);

    put_le32(g_buf + g_rsta_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_CHUNK_FLAGS);
}

static size_t build_gx_only(uint32_t entries, uint32_t flags) {
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    uint32_t i;

    for (i = 0; i < entries; i++) {
        put_gxcm_entry(i, TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    }
    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);
    return build_raw(g_gx, entries * 8u, meta, sizeof meta, NULL, 0u, flags,
                     0u);
}

static void test_gxcm_structure(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);

    size = build_raw(g_gx, 0u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_GXCM);

    put_gxcm_entry(0, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    size = build_raw(g_gx, 12u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_GXCM);

    size = build_gx_only(TWLHD_V2_GXCM_MAX_ENTRIES + 1u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_GXCM);

    size = build_gx_only(TWLHD_V2_GXCM_MAX_ENTRIES, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.gxcm_count, TWLHD_V2_GXCM_MAX_ENTRIES);
}

static void test_gxcm_cmd_words(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    static const uint32_t bad[] = {
        0x00000000u, /* NOP is not valid */
        0x00000042u,
        0x00000100u,
        0x00010005u,
        0x00030000u,
        0xFFFFFFFFu,
    };
    uint32_t i;
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        put_gxcm_entry(0, bad[i], 0u);
        size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
        CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                     TWLHD_V2_ERR_GXCM_CMD);
    }

    /* A RAW FIFO entry is opaque and needs no grouping. */
    put_gxcm_entry(0, TWLHD_V2_RAW_FIFO_BIT, 0x12345678u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, 0), TWLHD_V2_RAW_FIFO_BIT);
    CHECK_EQ_INT(twlhd_v2_gxcm_value(&view, 0), 0x12345678u);
}

static void test_gxcm_grouping(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);

    /* VTX_16 needs two entries. */
    put_gxcm_entry(0, TWLHD_V2_CMD_VTX_16, 0x1000u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_GROUP);

    /* Truncated trailing group is acceptable only with OVERFLOW. */
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_OVERFLOW, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    /* Same, but interrupted by a different command mid-group. */
    put_gxcm_entry(1, TWLHD_V2_CMD_COLOR, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_OVERFLOW, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_GROUP);

    /* RAW entry between group members breaks consecutiveness. */
    put_gxcm_entry(0, TWLHD_V2_CMD_VTX_16, 0u);
    put_gxcm_entry(1, TWLHD_V2_RAW_FIFO_BIT, 0u);
    put_gxcm_entry(2, TWLHD_V2_CMD_VTX_16, 0u);
    size = build_raw(g_gx, 24u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_OVERFLOW, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_GROUP);

    /* RAW entry at a group boundary is fine. */
    put_gxcm_entry(0, TWLHD_V2_CMD_VTX_16, 0u);
    put_gxcm_entry(1, TWLHD_V2_CMD_VTX_16, 0u);
    put_gxcm_entry(2, TWLHD_V2_RAW_FIFO_BIT, 0u);
    put_gxcm_entry(3, TWLHD_V2_CMD_COLOR, 0u);
    size = build_raw(g_gx, 32u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    /* Zero-parameter commands still consume one entry each. */
    put_gxcm_entry(0, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    put_gxcm_entry(1, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    /* MTX_SCALE / MTX_TRANS are three-entry groups. */
    put_gxcm_entry(0, TWLHD_V2_CMD_MTX_SCALE, 1u);
    put_gxcm_entry(1, TWLHD_V2_CMD_MTX_SCALE, 2u);
    put_gxcm_entry(2, TWLHD_V2_CMD_MTX_SCALE, 3u);
    put_gxcm_entry(3, TWLHD_V2_CMD_MTX_TRANS, 4u);
    put_gxcm_entry(4, TWLHD_V2_CMD_MTX_TRANS, 5u);
    put_gxcm_entry(5, TWLHD_V2_CMD_MTX_TRANS, 6u);
    size = build_raw(g_gx, 48u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
}

static void test_gxcm_frame_rule(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);

    /* Clean frame: FRAME_COMPLETE and SWAP last. */
    put_gxcm_entry(0, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    put_gxcm_entry(1, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_FRAME_COMPLETE, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    /* FRAME_COMPLETE without a trailing swap. */
    put_gxcm_entry(1, TWLHD_V2_CMD_END_VTXS, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_FRAME_COMPLETE, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_FRAME);

    /* Swap last, FRAME_COMPLETE clear. */
    put_gxcm_entry(1, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_FRAME);

    /* Swap not last. */
    put_gxcm_entry(0, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    put_gxcm_entry(1, TWLHD_V2_CMD_END_VTXS, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_FRAME);

    /* Two swaps. */
    put_gxcm_entry(0, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    put_gxcm_entry(1, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_FRAME_COMPLETE, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_FRAME);

    /* FRAME_COMPLETE with a RAW last entry. */
    put_gxcm_entry(0, TWLHD_V2_RAW_FIFO_BIT, 0u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u,
                     TWLHD_V2_FLAG_FRAME_COMPLETE, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_GXCM_FRAME);
}

static void test_rsta(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);
    put_gxcm_entry(0, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    put_gxcm_entry(1, TWLHD_V2_CMD_BEGIN_VTXS, 0u);

    /* Not a multiple of 16. */
    put_rsta_entry(0, 0x04000350u, 0u, 0u, 4u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs, 12u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_RSTA);

    /* Too many entries. */
    {
        uint32_t i;
        for (i = 0; i < TWLHD_V2_RSTA_MAX_ENTRIES + 1u; i++) {
            put_rsta_entry(i, 0x04000350u, 0u, 2u, 4u);
        }
    }
    size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs,
                     (TWLHD_V2_RSTA_MAX_ENTRIES + 1u) * 16u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_RSTA);

    /* Bad widths / alignment / range containment / gx_index. */
    {
        static const uint32_t bad[][4] = {
            {0x04000350u, 0u, 0u, 3u},       /* width 3            */
            {0x04000351u, 0u, 0u, 2u},       /* misaligned         */
            {0x04000300u, 0u, 0u, 4u},       /* not whitelisted    */
            {0x0400035Eu, 0u, 0u, 4u},       /* crosses ranges     */
            {0x04000612u, 0u, 0u, 1u},       /* one past the end   */
            {0x04000350u, 0u, 3u, 4u},       /* gx_index > count   */
            {0x04000350u, 0u, 0u, 4u},       /* valid baseline     */
        };
        uint32_t i;

        for (i = 0; i < 6u; i++) {
            put_rsta_entry(0, bad[i][0], bad[i][1], bad[i][2], bad[i][3]);
            size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs, 16u, 0u, 0u);
            CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                         TWLHD_V2_ERR_RSTA);
        }
    }

    /* Non-monotonic gx_index. */
    put_rsta_entry(0, 0x04000350u, 0u, 2u, 4u);
    put_rsta_entry(1, 0x04000354u, 0u, 1u, 2u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs, 32u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_RSTA);

    /* Whitelist edges accepted. */
    put_rsta_entry(0, 0x04000060u, 0u, 0u, 2u);
    put_rsta_entry(1, 0x0400033Fu, 0u, 1u, 1u);
    put_rsta_entry(2, 0x04000350u, 0u, 1u, 4u);
    put_rsta_entry(3, 0x04000610u, 0u, 2u, 2u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs, 64u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.rsta_count, 4);
    CHECK_EQ_INT(twlhd_v2_rsta_addr(&view, 0), 0x04000060u);
    CHECK_EQ_INT(twlhd_v2_rsta_width(&view, 2), 4u);
    CHECK_EQ_INT(twlhd_v2_rsta_gx_index(&view, 3), 2u);
    CHECK_EQ_INT(twlhd_v2_rsta_value(&view, 0), 0u);

    /* Values must be trimmed to the register width. */
    put_rsta_entry(0, 0x04000360u, 0x1FFu, 0u, 1u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs, 16u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_RSTA);

    put_rsta_entry(0, 0x04000380u, 0x1FFFFu, 0u, 2u);
    size = build_raw(g_gx, 16u, meta, sizeof meta, g_rs, 16u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_RSTA);

    /* An explicitly empty RSTA chunk is accepted and counted as present. */
    size = build_raw(g_gx, 16u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.rsta_count, 0);
}

static void test_meta(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_REGISTER_PROXY,
             TWLHD_V2_COVERAGE_LIBRARY_INTERNAL_WRITES, 0u, 0u);
    put_gxcm_entry(0, TWLHD_V2_CMD_BEGIN_VTXS, 0u);

    size = build_raw(g_gx, 8u, meta, 12u, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_META);

    put_meta(meta, TWLHD_V2_BACKEND_REGISTER_PROXY, 8u, 0u, 0u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_META);

    put_meta(meta, TWLHD_V2_BACKEND_REGISTER_PROXY, 0u, 1u, 0u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_META);

    put_meta(meta, TWLHD_V2_BACKEND_REGISTER_PROXY, 0u, 0u, 1u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_META);

    /* Unknown backend ids are informational, not a rejection. */
    put_meta(meta, 99u, 7u, 0u, 0u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.meta_backend_id, 99u);
    CHECK_EQ_INT(view.meta_coverage_flags, 7u);
}

static void test_view_fields(void) {
    TwlhdV2View view;
    size_t size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);

    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.total_size, size);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(view.frame_index, 1u);
    CHECK_EQ_INT(view.chunk_count, 3u);
    CHECK_EQ_INT(view.unknown_chunks_skipped, 0u);
    CHECK_EQ_INT(view.gxcm_count, 8u);
    CHECK_EQ_INT(view.rsta_count, 1u);
    CHECK_EQ_INT(view.meta_backend_id, TWLHD_V2_BACKEND_REGISTER_PROXY);
    CHECK_EQ_INT(view.meta_coverage_flags, 7u);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, 0), 0x10u);
    CHECK_EQ_INT(twlhd_v2_gxcm_value(&view, 0), 2u);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, 7), 0x50u);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, 8), 0u);
    CHECK_EQ_INT(twlhd_v2_rsta_addr(&view, 0), 0x04000350u);
    CHECK_EQ_INT(twlhd_v2_rsta_value(&view, 0), 0x001F3FFFu);
    CHECK_EQ_INT(twlhd_v2_rsta_gx_index(&view, 0), 3u);
    CHECK_EQ_INT(twlhd_v2_rsta_width(&view, 0), 4u);
    CHECK_EQ_INT(twlhd_v2_rsta_addr(&view, 1), 0u);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(NULL, 0), 0u);
    CHECK_EQ_INT(twlhd_v2_rsta_addr(NULL, 0), 0u);
}

static int view_is_zeroed(const TwlhdV2View *view) {
    const uint8_t *p = (const uint8_t *)view;
    size_t i;

    for (i = 0; i < sizeof *view; i++) {
        if (p[i] != 0u) return 0;
    }
    return 1;
}

static void poison_view(TwlhdV2View *view) {
    memset(view, 0xAB, sizeof *view);
}

static void test_view_zeroed_on_errors(void) {
    TwlhdV2View view;
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];
    size_t size;

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);

    /* A few representative early, mid and late error paths. */
    poison_view(&view);
    CHECK_EQ_INT(twlhd_v2_parse(NULL, 0, &view), TWLHD_V2_ERR_NULL);
    CHECK(view_is_zeroed(&view));

    poison_view(&view);
    put_le32(g_buf + 0, 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, 32u, &view), TWLHD_V2_ERR_VERSION);
    CHECK(view_is_zeroed(&view));
    put_le32(g_buf + 0, TWLHD_V2_VERSION);

    poison_view(&view);
    size = build_gx_only(1u, 0u);
    /* Keep total_size at the real value but pass one byte less. */
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size - 1u, &view),
                 TWLHD_V2_ERR_SIZE);
    CHECK(view_is_zeroed(&view));

    /* Late error: bad GXCM command id. */
    put_gxcm_entry(0, 0x42u, 0u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, NULL, 0u, 0u, 0u);
    poison_view(&view);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_GXCM_CMD);
    CHECK(view_is_zeroed(&view));

    /* Late error: bad RSTA value for width. */
    put_gxcm_entry(0, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    put_rsta_entry(0, 0x04000360u, 0x1FFu, 0u, 1u);
    size = build_raw(g_gx, 8u, meta, sizeof meta, g_rs, 16u, 0u, 0u);
    poison_view(&view);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_RSTA);
    CHECK(view_is_zeroed(&view));

    /* Success fully populates the view. */
    size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);
    poison_view(&view);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK(!view_is_zeroed(&view));
    CHECK_EQ_INT(view.total_size, size);
}

static void test_result_strings(void) {
    CHECK(strcmp(twlhd_v2_result_string(TWLHD_V2_OK), "OK") == 0);
    CHECK(strcmp(twlhd_v2_result_string(TWLHD_V2_ERR_CHUNK_BOUNDS),
                 "ERR_CHUNK_BOUNDS") == 0);
    CHECK(strcmp(twlhd_v2_result_string(TWLHD_V2_ERR_GXCM_GROUP),
                 "ERR_GXCM_GROUP") == 0);
    CHECK(strcmp(twlhd_v2_result_string(TWLHD_V2_ERR_UNSUPPORTED_REQUIRED),
                 "ERR_UNSUPPORTED_REQUIRED") == 0);
    CHECK(strcmp(twlhd_v2_result_string((TwlhdV2Result)999),
                 "ERR_UNKNOWN") == 0);
}

static void test_truncation_sweep(void) {
    TwlhdV2View view;
    size_t full = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);
    size_t s;

    for (s = 0; s < full; s++) {
        CHECK(twlhd_v2_parse(g_buf, s, &view) != TWLHD_V2_OK);
    }
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, full, &view), TWLHD_V2_OK);
}

static void test_byte_flip_sweep(void) {
    TwlhdV2View view;
    size_t full = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);
    size_t i;

    for (i = 0; i < full; i++) {
        TwlhdV2Result result;

        g_buf[i] ^= 0xFFu;
        result = twlhd_v2_parse(g_buf, full, &view);
        CHECK(twlhd_v2_result_string(result) != NULL);
        g_buf[i] ^= 0xFFu;
    }

    CHECK_EQ_INT(twlhd_v2_parse(g_buf, full, &view), TWLHD_V2_OK);
}

static void test_chunk_count_boundaries(void) {
    TwlhdV2View view;
    size_t size = build_golden(TWLHD_V2_FLAG_FRAME_COMPLETE);
    size_t offset;
    uint32_t i;

    /* chunk_count == 1 passes the count check, then reports the missing META. */
    put_le32(g_buf + 8, (uint32_t)g_meta_hdr);
    put_le32(g_buf + 20, 1u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, g_meta_hdr, &view),
                 TWLHD_V2_ERR_MISSING_CHUNK);
    put_le32(g_buf + 8, (uint32_t)size);
    put_le32(g_buf + 20, 3u);

    /* chunk_count == 16 is accepted; fill with optional unknown chunks. */
    offset = size;
    for (i = 0; i < 13u; i++) {
        offset = emit_chunk_header(offset, TWLHD_FOURCC('U', '0' + (char)i,
                                                       '0', '0'),
                                   4u, 0u);
        put_le32(g_buf + offset, i);
        offset += 4u;
    }
    put_le32(g_buf + 8, (uint32_t)offset);
    put_le32(g_buf + 20, 16u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, offset, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.chunk_count, 16u);
    CHECK_EQ_INT(view.unknown_chunks_skipped, 13u);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static size_t append_chunk(size_t end, uint32_t type, const uint8_t *payload,
                           uint32_t size, uint32_t chunk_flags) {
    size_t offset = emit_chunk_header(end, type, size, chunk_flags);

    if (size != 0u) memcpy(g_buf + offset, payload, size);
    offset += size;
    put_le32(g_buf + 8, (uint32_t)offset);
    put_le32(g_buf + 20, read_le32(g_buf + 20) + 1u);
    return offset;
}

/* GXCM(1 entry) + META + the given texture chunks. */
static size_t build_tex_default(const uint8_t *txds, uint32_t txds_size,
                                const uint8_t *txdt, uint32_t txdt_size,
                                const uint8_t *pldt, uint32_t pldt_size) {
    uint8_t meta[TWLHD_V2_META_PAYLOAD_SIZE];

    put_meta(meta, TWLHD_V2_BACKEND_SYNTHETIC, 0u, 0u, 0u);
    put_gxcm_entry(0, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    return build_tex(8u, meta, sizeof meta, txds, txds_size, txdt, txdt_size,
                     pldt, pldt_size, 0u, 0u);
}

static void test_tex(void) {
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    size_t size;
    uint32_t i;

    for (i = 0; i < sizeof g_txdt; i++) g_txdt[i] = (uint8_t)(0x40u + (i & 0x3Fu));
    for (i = 0; i < sizeof g_pldt; i++) g_pldt[i] = (uint8_t)(0x80u + (i & 0x3Fu));

    /* Positive: two I2 8x8 records, 16 texel + 8 palette bytes each. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    put_tex_record(1, 0x08000000u, 1u, 16u, 16u, 8u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 80u, g_txdt, 32u, g_pldt, 16u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 2u);
    CHECK_EQ_INT(view.txdt_size, 32u);
    CHECK_EQ_INT(view.pldt_size, 16u);
    CHECK_EQ_INT(view.unknown_chunks_skipped, 0u);
    CHECK_EQ_INT(twlhd_v2_txds_count(&view), 2u);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 1, &rec), 1);
    CHECK_EQ_INT(rec.teximage_param, 0x08000000u);
    CHECK_EQ_INT(rec.pltt_base, 1u);
    CHECK_EQ_INT(rec.texel_offset, 16u);
    CHECK_EQ_INT(rec.texel_size, 16u);
    CHECK_EQ_INT(rec.palette_offset, 8u);
    CHECK_EQ_INT(rec.palette_size, 8u);
    CHECK_EQ_INT(rec.index_offset, 0u);
    CHECK_EQ_INT(rec.index_size, 0u);
    CHECK(twlhd_v2_tex_texel_ptr(&view, 1) == view.txdt + 16);
    CHECK(twlhd_v2_tex_texel_ptr(&view, 1)[0] == g_txdt[16]);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 0) == view.pldt);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 1) == view.pldt + 8);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 1)[0] == g_pldt[8]);
    CHECK(twlhd_v2_tex_index_ptr(&view, 0) == NULL);
    CHECK(twlhd_v2_tex_texel_ptr(&view, 2) == NULL);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 2) == NULL);
    CHECK(twlhd_v2_txds_record(&view, 2, &rec) == 0);
    CHECK(twlhd_v2_txds_record(NULL, 0, &rec) == 0);
    CHECK(twlhd_v2_txds_record(&view, 0, NULL) == 0);
    CHECK_EQ_INT(twlhd_v2_txds_count(NULL), 0u);
    CHECK(twlhd_v2_tex_texel_ptr(NULL, 0) == NULL);
    CHECK(twlhd_v2_tex_palette_ptr(NULL, 0) == NULL);
    CHECK(twlhd_v2_tex_index_ptr(NULL, 0) == NULL);

    /* 4x4 record: a caller-chosen palette window plus the index block. */
    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 8u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(view.pldt_size, 8u);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 0, &rec), 1);
    CHECK_EQ_INT(rec.palette_offset, 0u);
    CHECK_EQ_INT(rec.palette_size, 8u);
    CHECK_EQ_INT(rec.index_offset, 16u);
    CHECK_EQ_INT(rec.index_size, 8u);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 0) == view.pldt);
    CHECK(twlhd_v2_tex_index_ptr(&view, 0) == view.txdt + 16);
    CHECK(twlhd_v2_tex_index_ptr(&view, 0)[0] == g_txdt[16]);

    /* 4x4 palette windows: even sizes in [2, 65536] are accepted. */
    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 2u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 4u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 65536u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 65536u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.pldt_size, 65536u);

    /* 4x4 negatives: zero, odd, and just past the upper bound. */
    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 0u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 3u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 65537u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 65540u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    /* 65,538 is even, so only the upper bound can reject it. */
    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 65538u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, g_pldt, 65540u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    /* A texture-free file keeps the old view exactly. */
    size = build_tex_default(NULL, 0u, NULL, 0u, NULL, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 0u);
    CHECK(view.txds == NULL);
    CHECK(view.txdt == NULL);
    CHECK(view.pldt == NULL);

    /* Structural: payload not a multiple of 40. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 20u, g_txdt, 16u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS);

    /* Structural: an explicitly empty TXDS chunk has a zero record count. */
    size = build_tex_default(NULL, 0u, g_txdt, 16u, g_pldt, 8u);
    size = append_chunk(size, TWLHD_V2_CHUNK_TXDS, NULL, 0u, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS);

    /* Structural: too many records. */
    for (i = 0; i < TWLHD_V2_TXDS_MAX_RECORDS + 1u; i++) {
        put_tex_record(i, 0x08000000u, i, 0u, 16u, 0u, 8u, 0u, 0u);
    }
    size = build_tex_default(g_txds, (TWLHD_V2_TXDS_MAX_RECORDS + 1u) * 40u,
                             g_txdt, 16u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS);

    /* Structural: nonzero reserved field. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    put_le32(g_txds + 32, 1u);
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS);
    put_le32(g_txds + 32, 0u);

    /* Structural: format 0. */
    put_tex_record(0, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS);

    /* Duplicate (teximage_param, pltt_base) key. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    put_tex_record(1, 0x08000000u, 0u, 16u, 16u, 8u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 80u, g_txdt, 32u, g_pldt, 16u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_KEY);

    /* Size formula mismatch. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 32u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 32u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    /* Offset must be zero when the matching size is zero. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 4u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    /* TXDT is required whenever TXDS is present. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, NULL, 0u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_MISSING_CHUNK);

    /* PLDT is required iff some record carries a palette. */
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, NULL, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_MISSING_CHUNK);

    /* A 4x4 record with palette_size 0 is no longer valid. */
    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 0u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, NULL, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDS_SIZE);

    /* A 4x4 palette window requires PLDT like every other palette. */
    put_tex_record(0, 5u << 26, 0u, 0u, 16u, 0u, 8u, 16u, 8u);
    size = build_tex_default(g_txds, 40u, g_txdt, 24u, NULL, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_MISSING_CHUNK);

    /* Bounds: past the end of TXDT. */
    put_tex_record(0, 0x08000000u, 0u, 24u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 32u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_TXDS_BOUNDS);

    /* Bounds: 64-bit overflow of offset + size. */
    put_tex_record(0, 0x08000000u, 0u, 0xFFFFFFF0u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 32u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_TXDS_BOUNDS);

    /* Bounds: past the end of PLDT. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 12u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt, 16u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_TXDS_BOUNDS);

    /* Duplicate TXDS / TXDT / PLDT chunks. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt, 8u);
    {
        size_t with_dup;

        with_dup = append_chunk(size, TWLHD_V2_CHUNK_TXDS, g_txds, 40u, 0u);
        CHECK_EQ_INT(twlhd_v2_parse(g_buf, with_dup, &view),
                     TWLHD_V2_ERR_DUP_CHUNK);
        with_dup = append_chunk(size, TWLHD_V2_CHUNK_TXDT, g_txdt, 16u, 0u);
        CHECK_EQ_INT(twlhd_v2_parse(g_buf, with_dup, &view),
                     TWLHD_V2_ERR_DUP_CHUNK);
        with_dup = append_chunk(size, TWLHD_V2_CHUNK_PLDT, g_pldt, 8u, 0u);
        CHECK_EQ_INT(twlhd_v2_parse(g_buf, with_dup, &view),
                     TWLHD_V2_ERR_DUP_CHUNK);
    }

    /* REQUIRED must stay clear on the new optional chunks. */
    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt, 8u);
    put_le32(g_buf + g_txds_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_txds_hdr + 8, 0u);
    put_le32(g_buf + g_txdt_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_txdt_hdr + 8, 0u);
    put_le32(g_buf + g_pldt_hdr + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_CHUNK_FLAGS);
    put_le32(g_buf + g_pldt_hdr + 8, 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);

    /* The view stays zeroed on a late texture error. */
    put_tex_record(0, 0x08000000u, 0u, 24u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt, 32u, g_pldt, 8u);
    poison_view(&view);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view),
                 TWLHD_V2_ERR_TXDS_BOUNDS);
    CHECK(view_is_zeroed(&view));
}

static void test_tex_limits(void) {
    TwlhdV2View view;
    size_t size;

    /* Record references the first 16 bytes only, so bounds pass. */
    put_tex_record(0, 0x08000000u, 0u, 0u, 16u, 0u, 8u, 0u, 0u);
    size = build_tex_default(g_txds, 40u, g_txdt,
                             TWLHD_V2_TXDT_MAX_BYTES + 4u, g_pldt, 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_TXDT);

    size = build_tex_default(g_txds, 40u, g_txdt, 16u, g_pldt,
                             TWLHD_V2_PLDT_MAX_BYTES + 4u);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_ERR_PLDT);

    /* The exact limits are accepted. */
    size = build_tex_default(g_txds, 40u, g_txdt, TWLHD_V2_TXDT_MAX_BYTES,
                             g_pldt, TWLHD_V2_PLDT_MAX_BYTES);
    CHECK_EQ_INT(twlhd_v2_parse(g_buf, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txdt_size, TWLHD_V2_TXDT_MAX_BYTES);
    CHECK_EQ_INT(view.pldt_size, TWLHD_V2_PLDT_MAX_BYTES);
}

int main(void) {
    test_null_version_truncated();
    test_report("v2-parse/null-version-truncated");
    test_header();
    test_report("v2-parse/header");
    test_chunk_structure();
    test_report("v2-parse/chunk-structure");
    test_chunks();
    test_report("v2-parse/chunks");
    test_chunk_count_boundaries();
    test_report("v2-parse/chunk-count-boundaries");
    test_gxcm_structure();
    test_report("v2-parse/gxcm-structure");
    test_gxcm_cmd_words();
    test_report("v2-parse/gxcm-cmd-words");
    test_gxcm_grouping();
    test_report("v2-parse/gxcm-grouping");
    test_gxcm_frame_rule();
    test_report("v2-parse/gxcm-frame-rule");
    test_rsta();
    test_report("v2-parse/rsta");
    test_meta();
    test_report("v2-parse/meta");
    test_tex();
    test_report("v2-parse/tex");
    test_tex_limits();
    test_report("v2-parse/tex-limits");
    test_view_zeroed_on_errors();
    test_report("v2-parse/view-zeroed-on-errors");
    test_view_fields();
    test_report("v2-parse/view-fields");
    test_result_strings();
    test_report("v2-parse/result-strings");
    test_truncation_sweep();
    test_report("v2-parse/truncation-sweep");
    test_byte_flip_sweep();
    test_report("v2-parse/byte-flip-sweep");
    return test_exit_code();
}
