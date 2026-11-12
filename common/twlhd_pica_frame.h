// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2e-1 - host-side "textured frame preparation" for the
 * PICA200.
 *
 * One allocation-free call glues the existing pieces together: it takes a
 * parsed v2 frame view (TwlhdV2View with TXDS/TXDT/PLDT), the interpreter's
 * per-vertex texture latch (TwlhdGxTexVertex), and the caller's buffers, and
 * produces everything a renderer needs for textured triangles:
 *
 *   (a) bind-plan entries                      twlhd_pica_tex_bind_plan()
 *   (b) decoded + swizzled PICA RGBA8 textures  twlhd_tex_decode() +
 *                                               twlhd_pica_tex_swizzle_rgba8()
 *   (c) an 8-byte-per-vertex texcoord array     twlhd_pica_texcoords_from_gx()
 *   (d) entry -> texture index lookup          this module
 *
 * BUILD/UNIT only: no GPU calls and no hardware verification. The facts about
 * the texture layout come from docs/DS_GX_RESEARCH.md, "PICA texture upload
 * (5B-2d)" and "Textured frame preparation (5B-2e-1)".
 *
 * Memory budget (numbers, not upper bounds on caller buffers):
 *
 *   - one 1024x1024 RGBA8 texture ............ 4,194,304 B (4 MiB)
 *   - worst-case arena, 256 TXDS records ..... 1,073,741,824 B (1 GiB)
 *       All 256 records may point at the same TXDT bytes (the parser is lax
 *       about overlapping offsets, docs/TWLHD_V2_FORMAT.md:369-372); 256
 *       distinct (TEXIMAGE_PARAM, PLTT_BASE) keys can therefore each decode a
 *       1024x1024 I2 texture out of the same 262,144-byte block.
 *   - non-overlapping worst case ............. 8,388,608 B (8 MiB)
 *       TXDT is capped at 524,288 B and the largest expansion (I2/I4x4 at
 *       1/4 byte per texel, 16x into RGBA8) gives 8 MiB. Texture byte sizes
 *       are multiples of 256, so appending them never needs padding: only the
 *       arena base must be 128-byte aligned (UNVERIFIED for real hardware).
 *   - scratch (decode target before the swizzle) . 4 MiB for one texture.
 *   - texcoords: 65,536 x 8 B = 524,288 B (512 KiB).
 *   - bind plan: 21,845 entries x 20 B ~= 437 KiB.
 *
 * Every error path leaves the caller's output buffers untouched. The scratch
 * buffer is a work buffer, not an output: its contents are undefined on return
 * (including on error), because the validation pass decodes through it. No
 * dynamic allocation happens anywhere in this module.
 */
#pragma once

#include <stdint.h>

#include "twlhd_gx_interp.h"
#include "twlhd_pica_tex.h"
#include "twlhd_v2_format.h"
#include "twlhd_v2_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TWLHD_PICA_FRAME_OK = 0,
    TWLHD_PICA_FRAME_ERR_NULL,     /* a required pointer is NULL */
    TWLHD_PICA_FRAME_ERR_CAPACITY, /* an output buffer / scratch is too small */
    TWLHD_PICA_FRAME_ERR_ARENA,    /* arena_capacity < the exact summed need */
    TWLHD_PICA_FRAME_ERR_COUNT     /* vertex_count is not a multiple of 3 */
} TwlhdPicaFrameResult;

/* Per-plan-entry status. A failed entry still occupies a plan slot. */
typedef enum {
    TWLHD_PICA_FRAME_ENTRY_OK = 0,
    /* No TXDS record with this (teximage_param, pltt_base), or its data
     * pointers are absent / out of bounds. */
    TWLHD_PICA_FRAME_ENTRY_NO_DATA,
    /* The record exists but the decoder or the PICA size check refuses it. */
    TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED
} TwlhdPicaFrameEntryStatus;

/* One stored texture. `offset` is a byte offset into the caller's arena. */
typedef struct {
    uint32_t offset;
    uint32_t width;
    uint32_t height;
    TwlhdPicaWrap wrap_s;
    TwlhdPicaWrap wrap_t;
} TwlhdPicaFrameTexture;

/*
 * Recommended defaults, matching the v2 capture caps. A caller may pass any
 * capacities directly to twlhd_pica_frame_prepare(); this struct only names
 * the worst cases so they are not invented per call site.
 */
typedef struct {
    uint32_t max_textures; /* TWLHD_PICA_FRAME_MAX_TEXTURES (256) */
    uint32_t arena_bytes;  /* TWLHD_PICA_FRAME_ARENA_MAX_BYTES (1 GiB) */
    uint32_t max_texcoords;/* TWLHD_PICA_FRAME_MAX_TEXCOORDS (65536) */
} TwlhdPicaFrameLimits;

#define TWLHD_PICA_FRAME_MAX_TEXTURES TWLHD_V2_TXDS_MAX_RECORDS   /* 256 */
#define TWLHD_PICA_FRAME_MAX_TEXCOORDS TWLHD_V2_GXCM_MAX_ENTRIES /* 65536 */
#define TWLHD_PICA_FRAME_MAX_TEXTURE_BYTES (1024u * 1024u * 4u)  /* 4 MiB */
#define TWLHD_PICA_FRAME_SCRATCH_BYTES TWLHD_PICA_FRAME_MAX_TEXTURE_BYTES
#define TWLHD_PICA_FRAME_ARENA_MAX_BYTES (1073741824u)            /* 1 GiB */
#define TWLHD_PICA_FRAME_ARENA_PRACTICAL_BYTES (8388608u)         /* 8 MiB */

/* Fill *out with the recommended defaults; ignored when out is NULL. */
void twlhd_pica_frame_default_limits(TwlhdPicaFrameLimits *out);

/*
 * Prepare one frame.
 *
 * tex/vertex_count: interpreter output latches. vertex_count must be a
 *   multiple of 3. The bind-plan run key is the triangle's first vertex, i.e.
 *   tex[3k] for triangle k (same rule as twlhd_pica_bind_plan()).
 * view: parsed v2 frame; texture data is looked up by the exact
 *   (teximage_param, pltt_base) pair of a TXDS record. Untextured runs
 *   (PackedFormat bits 26-28 == 0) need no record.
 *
 * Outputs (all required, all untouched when the call fails):
 *   plan/plan_capacity:             bind plan, capacity in entries.
 *   textures/texture_capacity:     deduplicated textures, capacity in entries.
 *   arena/arena_capacity:          packed texture bytes. The base must be
 *                                  128-byte aligned (UNVERIFIED assumption);
 *                                  every texture size is a multiple of 256, so
 *                                  no inter-texture padding is inserted.
 *   texcoords/texcoord_capacity:   vertex_count 8-byte coordinates. Entries
 *                                  whose texture is untextured or failed get
 *                                  (0.0f, 0.0f).
 *   entry_texture/entry_status:    parallel to the plan (entry_capacity
 *                                  entries). entry_texture is -1 for
 *                                  untextured/failed entries.
 *   scratch/scratch_size:          at least the largest single decoded
 *                                  texture (width*height*4). Contents are
 *                                  undefined on return.
 *   out_entry_count/out_texture_count/out_arena_used: exact counts on success.
 *
 * Per-entry failures (NO_DATA / UNSUPPORTED) do not fail the frame: the entry
 * keeps its plan slot, gets index -1 and zeroed texcoords. NULL, capacity,
 * arena and count errors fail the whole call.
 */
TwlhdPicaFrameResult twlhd_pica_frame_prepare(
    const TwlhdGxTexVertex *tex, uint32_t vertex_count,
    const TwlhdV2View *view,
    TwlhdPicaBindPlan *plan, uint32_t plan_capacity,
    TwlhdPicaFrameTexture *textures, uint32_t texture_capacity,
    uint8_t *arena, uint32_t arena_capacity,
    TwlhdPicaTexCoord *texcoords, uint32_t texcoord_capacity,
    int32_t *entry_texture, TwlhdPicaFrameEntryStatus *entry_status,
    uint32_t entry_capacity,
    uint8_t *scratch, uint32_t scratch_size,
    uint32_t *out_entry_count, uint32_t *out_texture_count,
    uint32_t *out_arena_used);

/*
 * Pure sizing pass: returns the exact number of arena bytes that
 * twlhd_pica_frame_prepare() will consume for the same inputs (equal to
 * *out_arena_used on success). Writes only scratch and *out_arena_bytes.
 * Fails with ERR_NULL / ERR_COUNT / ERR_CAPACITY (scratch too small for a
 * texture that would decode).
 */
TwlhdPicaFrameResult twlhd_pica_frame_arena_needed(
    const TwlhdGxTexVertex *tex, uint32_t vertex_count,
    const TwlhdV2View *view, uint8_t *scratch, uint32_t scratch_size,
    uint32_t *out_arena_bytes);

const char *twlhd_pica_frame_result_string(TwlhdPicaFrameResult result);

const char *twlhd_pica_frame_entry_status_string(
    TwlhdPicaFrameEntryStatus status);

#ifdef __cplusplus
}
#endif
