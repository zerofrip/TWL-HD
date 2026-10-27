/*
 * TWL-HD Phase 1 - ARM11 status screen.
 *
 * The RTCom bridge to the ARM9 capture hook is not implemented yet, so this
 * application only reports the build status and the Phase 3 roadmap item.
 */

#include <3ds.h>
#include <stdio.h>

#include "rtcom_transport.h"

int main(void) {
    int transport_status;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    transport_status = rtcom_transport_init();

    printf("TWL-HD research build\n");
    printf("Version: 0.1.0-phase1\n\n");

    if (transport_status == TWLHD_ERR_NOT_IMPLEMENTED) {
        printf("RTCom transport: not implemented\n");
        printf("Frame receiver: NOT functional\n\n");
        printf("ARM9 capture (.nds) is Phase 1.\n");
        printf("RTCom bridging is Phase 3.\n\n");
    } else {
        printf("RTCom transport: init returned %d\n\n", transport_status);
    }

    printf("Press START to exit.\n");

    while (aptMainLoop()) {
        hidScanInput();

        if (hidKeysDown() & KEY_START) break;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
