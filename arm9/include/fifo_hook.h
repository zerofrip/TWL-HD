#ifndef FIFO_HOOK_H
#define FIFO_HOOK_H

#include <stdint.h>

#include "../../common/twlhd_frame_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once, after glInit() and before any captured drawing. */
void fifo_hook_init(void);

/* Recording is only active between these two calls. */
void fifo_hook_start_capture(void);
void fifo_hook_stop_capture(void);

/* Last captured frame (owned by the hook, do not free). */
const TwlhdFrame *fifo_hook_get_frame(void);

/* Start a new frame: bumps frame_index and clears cmd_count. */
void fifo_hook_reset_frame(void);

/*
 * Thin wrappers around the libnds 3D calls that map 1:1 onto GXFIFO commands.
 * Each one calls the real libnds function and then records the decoded
 * command. Drawing code should use these while capture is enabled.
 */
void fifo_hook_glBegin(uint32_t primitive);
void fifo_hook_glEnd(void);
void fifo_hook_glVertex3v16(int16_t x, int16_t y, int16_t z);
void fifo_hook_glColor3b(uint8_t red, uint8_t green, uint8_t blue);
void fifo_hook_glNormal(uint32_t packed_normal);
void fifo_hook_glTexCoord2t16(int16_t u, int16_t v);
void fifo_hook_glMatrixMode(uint32_t mode);
void fifo_hook_glPushMatrix(void);
void fifo_hook_glPopMatrix(uint32_t num);
void fifo_hook_glLoadIdentity(void);
void fifo_hook_glViewport(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);

/*
 * Overridable transport hook (Phase 3). The weak default does nothing, so a
 * later RTCom implementation can provide its own definition without touching
 * the capture code.
 */
void fifo_hook_transport_send(const TwlhdFrame *frame);

#ifdef __cplusplus
}
#endif

#endif /* FIFO_HOOK_H */
