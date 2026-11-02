// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5A-2 - DS clip space to PICA200 clip space fixup matrix.
 *
 * Pure C11, allocation-free, host-tested. No libc beyond what
 * twlhd_gx_interp.h already pulls in (<stddef.h>, <stdint.h>).
 *
 * The v2 interpreter emits vertices in DS clip space (x, y, z, w). The GPU
 * applies this single 4x4 uniform after the vertex transform:
 *
 *   x' =  hh * y + cy * w
 *   y' = -0.8 * (hw * x + cx * w)
 *   z' = (z - w) / 2
 *   w' =  w
 *
 * Derivation (see docs/PHASE5A_V2_RENDERER_DESIGN.md sections 4-5):
 *   - DS viewport is 256x192 with y measured from the bottom; DeSmuME's
 *     GFX3D_ViewportParse/transform maps clip space through the viewport
 *     rectangle, so the offset is (2*x1 + width)/256 - 1 and the half-extent
 *     is width/256 (likewise for y with 192).
 *   - citro3d Mtx_PerspTilt / Mtx_Persp bake a quarter turn into the
 *     projection: x' = y_std, y' = -x_std. That is the swap and the sign.
 *   - The 320x240 top-screen region inside the 400x240 target pillar-boxes
 *     the 256x192 image by 0.8 (256/320).
 *   - citro3d projections target PICA200 clip z in [-w, 0] while the DS uses
 *     [-w, w], hence z' = (z - w) / 2 (-w maps to -w, +w maps to 0).
 *
 * A NULL viewport or one without `present` means the full DS viewport
 * (0,0)-(255,191), which yields the identity-like matrix above with
 * cx = cy = 0 and hw = hh = 1.
 */
#pragma once

#include "twlhd_gx_interp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Write the row-major 4x4 clip fixup matrix for `vp` into `m`.
 *
 * m must point to at least 4 rows of 4 floats.
 */
void twlhd_clipfix_matrix(const TwlhdGxViewport *vp, float m[4][4]);

#ifdef __cplusplus
}
#endif
