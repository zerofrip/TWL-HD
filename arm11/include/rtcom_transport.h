#pragma once

#include <stdint.h>

#include "../../common/twlhd_frame_format.h"

#define TWLHD_OK                   0
#define TWLHD_ERR_NOT_IMPLEMENTED  (-1)

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 3: bring up the RTCom channel to the ARM9. */
int rtcom_transport_init(void);

/* Phase 3: block until a frame arrives and copy it into out_frame. */
int rtcom_transport_recv_frame(TwlhdFrame *out_frame);

#ifdef __cplusplus
}
#endif
