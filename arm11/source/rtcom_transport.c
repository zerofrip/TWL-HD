/*
 * TWL-HD Phase 1 - RTCom transport stub.
 *
 * Phase 1 only captures commands on the ARM9. The ARM9 <-> ARM11 channel is a
 * Phase 3 work item, so every entry point reports "not implemented" and no
 * CPU wakeups or shared-memory writes happen yet.
 */

#include "rtcom_transport.h"

/* TODO Phase 3: implement the actual RTCom transport here. */

int rtcom_transport_init(void) {
    return TWLHD_ERR_NOT_IMPLEMENTED;
}

int rtcom_transport_recv_frame(TwlhdFrame *out_frame) {
    (void)out_frame;
    return TWLHD_ERR_NOT_IMPLEMENTED;
}
