// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_gx_record.c.
 */

#include "twlhd_gx_record.h"

#include "tests_host.h"

static TwlhdGxRecorder g_rec;
static uint8_t g_out[TWLHD_V2_FILE_MAX];

typedef struct {
    uint32_t addr;
    uint32_t id;
} PortMap;

static const PortMap g_ports[] = {
    {0x04000440u, 0x10u}, {0x04000444u, 0x11u}, {0x04000448u, 0x12u},
    {0x0400044Cu, 0x13u}, {0x04000450u, 0x14u}, {0x04000454u, 0x15u},
    {0x04000458u, 0x16u}, {0x0400045Cu, 0x17u}, {0x04000460u, 0x18u},
    {0x04000464u, 0x19u}, {0x04000468u, 0x1Au}, {0x0400046Cu, 0x1Bu},
    {0x04000470u, 0x1Cu}, {0x04000480u, 0x20u}, {0x04000484u, 0x21u},
    {0x04000488u, 0x22u}, {0x0400048Cu, 0x23u}, {0x04000490u, 0x24u},
    {0x04000494u, 0x25u}, {0x04000498u, 0x26u}, {0x0400049Cu, 0x27u},
    {0x040004A0u, 0x28u}, {0x040004A4u, 0x29u}, {0x040004A8u, 0x2Au},
    {0x040004ACu, 0x2Bu}, {0x040004C0u, 0x30u}, {0x040004C4u, 0x31u},
    {0x040004C8u, 0x32u}, {0x040004CCu, 0x33u}, {0x040004D0u, 0x34u},
    {0x04000500u, 0x40u}, {0x04000504u, 0x41u}, {0x04000580u, 0x60u},
    {0x040005C0u, 0x70u}, {0x040005C4u, 0x71u}, {0x040005C8u, 0x72u},
};

static void test_not_armed_and_port_map(void) {
    uint32_t i;

    twlhd_gx_rec_init(&g_rec);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 0);

    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 1u);

    twlhd_gx_rec_arm(&g_rec);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 1);

    for (i = 0; i < sizeof g_ports / sizeof g_ports[0]; i++) {
        twlhd_gx_rec_write(&g_rec, g_ports[i].addr, 0xA5A50000u | i, 4u);
        CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), i + 1u);
        CHECK_EQ_INT(g_rec.gxcm[i].cmd_word, g_ports[i].id);
        CHECK_EQ_INT(g_rec.gxcm[i].value, 0xA5A50000u | i);
    }

    /* SWAP_BUFFERS closes the frame; it is valid as the last entry. */
    twlhd_gx_rec_write(&g_rec, 0x04000540u, 0u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 0);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_FRAME_COMPLETE,
                 1u);
    CHECK_EQ_INT(g_rec.gxcm[g_rec.gxcm_count - 1u].cmd_word, 0x50u);

    /* Further writes while disarmed are ignored. */
    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 1u);
}

static void test_raw_and_invalid(void) {
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_gx_rec_write(&g_rec, 0x04000400u, 0xDEADBEEFu, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 1u);
    CHECK_EQ_INT(g_rec.gxcm[0].cmd_word, TWLHD_V2_RAW_FIFO_BIT);
    CHECK_EQ_INT(g_rec.gxcm[0].value, 0xDEADBEEFu);

    /* Address in the command window with an invalid command id (0x1D). */
    twlhd_gx_rec_write(&g_rec, 0x04000474u, 1u, 4u);
    /* Wrong width on a command port. */
    twlhd_gx_rec_write(&g_rec, 0x04000440u, 1u, 2u);
    /* Misaligned address with a 4-byte width. */
    twlhd_gx_rec_write(&g_rec, 0x04000442u, 1u, 4u);
    /* Below the command window: ignored, not "invalid". */
    twlhd_gx_rec_write(&g_rec, 0x04000404u, 1u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 1u);
    CHECK_EQ_INT(twlhd_gx_rec_invalid_writes(&g_rec), 3u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 1u);

    /* FIFO with a non-32-bit width is ignored, not recorded. */
    twlhd_gx_rec_write(&g_rec, 0x04000400u, 1u, 2u);
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 1u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 2u);
}

static void test_rsta(void) {
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    twlhd_gx_rec_write(&g_rec, 0x04000350u, 0x001F3FFFu, 4u);
    twlhd_gx_rec_write(&g_rec, 0x04000354u, 0x7FFFu, 2u);
    twlhd_gx_rec_write(&g_rec, 0x04000060u, 0x0001u, 2u);
    twlhd_gx_rec_write(&g_rec, 0x04000360u, 0x1Fu, 1u);

    CHECK_EQ_INT(twlhd_gx_rec_rsta_count(&g_rec), 4u);
    CHECK_EQ_INT(g_rec.rsta[0].addr, 0x04000350u);
    CHECK_EQ_INT(g_rec.rsta[0].gx_index, 1u);
    CHECK_EQ_INT(g_rec.rsta[0].width, 4u);
    CHECK_EQ_INT(g_rec.rsta[1].gx_index, 1u);
    CHECK_EQ_INT(g_rec.rsta[2].addr, 0x04000060u);
    CHECK_EQ_INT(g_rec.rsta[3].width, 1u);

    /* Values wider than the port width are masked to the width. */
    twlhd_gx_rec_write(&g_rec, 0x04000360u, 0x1FFu, 1u);
    twlhd_gx_rec_write(&g_rec, 0x04000380u, 0x1FFFFu, 2u);
    CHECK_EQ_INT(g_rec.rsta[4].value, 0xFFu);
    CHECK_EQ_INT(g_rec.rsta[5].value, 0xFFFFu);

    /* Non-whitelisted / badly aligned RSTA writes are ignored. */
    twlhd_gx_rec_write(&g_rec, 0x04000300u, 1u, 4u);
    twlhd_gx_rec_write(&g_rec, 0x04000351u, 1u, 2u);
    twlhd_gx_rec_write(&g_rec, 0x04000350u, 1u, 3u);
    CHECK_EQ_INT(twlhd_gx_rec_rsta_count(&g_rec), 6u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 3u);
}

static void test_complete_serialize(void) {
    TwlhdV2View view;
    size_t size = 0;
    TwlhdV2Result result;

    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_gx_rec_write(&g_rec, 0x04000440u, 2u, 4u);      /* 0x10 */
    twlhd_gx_rec_write(&g_rec, 0x04000454u, 0u, 4u);      /* 0x15 */
    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);      /* 0x40 */
    twlhd_gx_rec_write(&g_rec, 0x04000480u, 0x1Fu, 4u);   /* 0x20 */
    twlhd_gx_rec_write(&g_rec, 0x0400048Cu, 0x1000u, 4u); /* 0x23 */
    twlhd_gx_rec_write(&g_rec, 0x0400048Cu, 0x2000u, 4u); /* 0x23 */
    twlhd_gx_rec_write(&g_rec, 0x04000504u, 0u, 4u);      /* 0x41 */
    twlhd_gx_rec_write(&g_rec, 0x04000350u, 0x001F3FFFu, 4u);
    twlhd_gx_rec_write(&g_rec, 0x04000540u, 0u, 4u);      /* 0x50 */

    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec), TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 0);

    result = twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size);
    CHECK_EQ_INT(result, TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.gxcm_count, 8u);
    CHECK_EQ_INT(view.rsta_count, 1u);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(view.frame_index, 1u);
    CHECK_EQ_INT(view.meta_backend_id, TWLHD_V2_BACKEND_REGISTER_PROXY);
    CHECK_EQ_INT(twlhd_v2_rsta_addr(&view, 0), 0x04000350u);
    CHECK_EQ_INT(twlhd_v2_rsta_gx_index(&view, 0), 7u);
    CHECK_EQ_INT(twlhd_v2_rsta_value(&view, 0), 0x001F3FFFu);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, 0), 0x10u);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, 7), 0x50u);

    /* Re-arm clears everything and advances the frame index. */
    twlhd_gx_rec_arm(&g_rec);
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_rsta_count(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_invalid_writes(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_entries(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_rsta(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 1);
    CHECK_EQ_INT(twlhd_gx_rec_frame_index(&g_rec), 2u);

    /* Empty frame cannot be serialized. */
    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_GXCM);
}

static void test_gxcm_overflow(void) {
    TwlhdV2View view;
    size_t size = 0;
    uint32_t i;

    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    for (i = 0; i < TWLHD_GX_REC_MAX_ENTRIES; i++) {
        twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    }
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), TWLHD_GX_REC_MAX_ENTRIES);
    CHECK_EQ_INT(twlhd_gx_rec_overflowed(&g_rec), 0);

    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_entries(&g_rec), 1u);
    CHECK_EQ_INT(twlhd_gx_rec_overflowed(&g_rec), 1);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_OVERFLOW, 2u);

    /* SWAP after any drop is itself dropped: no FRAME_COMPLETE. */
    twlhd_gx_rec_write(&g_rec, 0x04000540u, 0u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_entries(&g_rec), 2u);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 0);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_FRAME_COMPLETE,
                 0u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_OVERFLOW, 2u);

    /* The overflowed frame still serializes: complete groups + OVERFLOW. */
    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags,
                 TWLHD_V2_FLAG_OVERFLOW);
    CHECK_EQ_INT(view.gxcm_count, TWLHD_GX_REC_MAX_ENTRIES);
}

static void test_rsta_overflow(void) {
    TwlhdV2View view;
    size_t size = 0;
    uint32_t i;

    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    for (i = 0; i < TWLHD_GX_REC_MAX_RSTA; i++) {
        twlhd_gx_rec_write(&g_rec, 0x04000350u, i, 4u);
    }
    CHECK_EQ_INT(twlhd_gx_rec_rsta_count(&g_rec), TWLHD_GX_REC_MAX_RSTA);
    CHECK_EQ_INT(twlhd_gx_rec_overflowed(&g_rec), 0);

    twlhd_gx_rec_write(&g_rec, 0x04000350u, 0xFFFFFFFFu, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_rsta(&g_rec), 1u);
    CHECK_EQ_INT(twlhd_gx_rec_overflowed(&g_rec), 1);

    twlhd_gx_rec_write(&g_rec, 0x04000540u, 0u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 0);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_FRAME_COMPLETE,
                 0u);

    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_OVERFLOW);
    CHECK_EQ_INT(view.rsta_count, TWLHD_GX_REC_MAX_RSTA);
    CHECK_EQ_INT(view.gxcm_count, 1u);
}

static void test_serialize_capacity_and_global(void) {
    TwlhdV2View view;
    size_t size = 0;
    TwlhdV2Result result;
    TwlhdGxRecorder *global = twlhd_gx_rec_global();

    CHECK(TWLHD_GX_REC_SERIALIZE_MAX == 135264u);

    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);
    twlhd_gx_rec_write(&g_rec, 0x04000500u, 1u, 4u);
    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, 32u, &size),
                 TWLHD_V2_ERR_SIZE);

    CHECK_EQ_INT(twlhd_gx_rec_serialize(NULL, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_rsta_count(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_frame_index(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_invalid_writes(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_entries(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_dropped_rsta(NULL), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(NULL), 0);
    CHECK_EQ_INT(twlhd_gx_rec_overflowed(NULL), 0);

    twlhd_gx_rec_init(global);
    twlhd_gx_rec_arm(global);
    twlhd_gx_rec_hook(0x04000500u, 1u, 4u);
    twlhd_gx_rec_hook(0x04000540u, 0u, 4u);
    result = twlhd_gx_rec_serialize(global, g_out, sizeof g_out, &size);
    CHECK_EQ_INT(result, TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.gxcm_count, 2u);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
}

int main(void) {
    test_not_armed_and_port_map();
    test_report("gx-record/port-map");
    test_raw_and_invalid();
    test_report("gx-record/raw-invalid");
    test_rsta();
    test_report("gx-record/rsta");
    test_complete_serialize();
    test_report("gx-record/complete-serialize");
    test_gxcm_overflow();
    test_report("gx-record/gxcm-overflow");
    test_rsta_overflow();
    test_report("gx-record/rsta-overflow");
    test_serialize_capacity_and_global();
    test_report("gx-record/serialize-global");
    return test_exit_code();
}
