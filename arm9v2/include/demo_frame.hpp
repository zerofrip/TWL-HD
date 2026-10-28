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

#ifdef __cplusplus
extern "C" {
#endif

void twlhd_demo_draw_frame(unsigned angle);

#ifdef __cplusplus
}
#endif
