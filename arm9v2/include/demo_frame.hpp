// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - self-contained v2 capture demo frame body.
 *
 * This function is compiled by both the arm9v2 demo and a host test. The caller
 * is responsible for arming the recorder; twlhd_demo_draw_frame() issues every
 * piece of state required to replay the frame (except what libnds' glInit()
 * sets, declared via INITIAL_STATE_UNKNOWN) and ends with glFlush(0), which
 * records SWAP_BUFFERS and disarms the recorder.
 */
#pragma once

#include "twlhd_tex_source.h"

#ifdef __cplusplus
extern "C" {
#endif

void twlhd_demo_draw_frame(unsigned angle);

#if TWLHD_DEMO_TEXBEH
/* Upload the texture-behaviour grid (texels, palettes) once. */
void twlhd_demo_texbeh_upload_textures(void);
#endif

#if TWLHD_DEMO_TEXMOD
/* Upload the MODULATE probe textures once. */
void twlhd_demo_texmod_upload_textures(void);
#endif

#if TWLHD_DEMO_TEXFMT
/* Upload the texture-format grid (texels, 4x4 indices, palettes) once. */
void twlhd_demo_texfmt_upload_textures(void);
#endif

#if TWLHD_DEMO_SCENE == 5
/* Register the scene-5 texture/palette source arrays with the capture backend. */
void twlhd_demo_scene5_register_sources(TwlhdTexSourceCtx *ctx);
/* Upload scene-5 texture/palette to VRAM once at init. */
void twlhd_demo_scene5_upload_textures(void);
#endif

#ifdef __cplusplus
}
#endif
