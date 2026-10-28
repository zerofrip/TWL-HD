// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B/4C repair - active/pending vertex-buffer state machine.
 */

#include <string.h>

#include "twlhd_geometry_slots.h"

void twlhd_geometry_slots_init(TwlhdGeometrySlots *slots,
                               TwlhdGeometryAllocFn alloc_fn,
                               TwlhdGeometryFreeFn free_fn) {
    memset(slots, 0, sizeof(*slots));
    slots->alloc = alloc_fn;
    slots->free = free_fn;
}

int twlhd_geometry_slots_stage(TwlhdGeometrySlots *slots,
                               const void *data, size_t size) {
    void *new_pending = NULL;

    if (size != 0 && data == NULL) return -1;

    if (size != 0) {
        new_pending = slots->alloc(size);
        if (!new_pending) return -1;
        memcpy(new_pending, data, size);
    }

    if (slots->pending) {
        slots->free(slots->pending);
    }
    slots->pending = new_pending;
    slots->pending_size = size;
    return 0;
}

void twlhd_geometry_slots_commit(TwlhdGeometrySlots *slots) {
    if (!slots->pending) return;

    if (slots->active) {
        slots->free(slots->active);
    }
    slots->active = slots->pending;
    slots->active_size = slots->pending_size;
    slots->pending = NULL;
    slots->pending_size = 0;
}

void twlhd_geometry_slots_release_pending(TwlhdGeometrySlots *slots) {
    if (!slots->pending) return;
    slots->free(slots->pending);
    slots->pending = NULL;
    slots->pending_size = 0;
}

void twlhd_geometry_slots_release_active(TwlhdGeometrySlots *slots) {
    if (!slots->active) return;
    slots->free(slots->active);
    slots->active = NULL;
    slots->active_size = 0;
}

void twlhd_geometry_slots_release_all(TwlhdGeometrySlots *slots) {
    twlhd_geometry_slots_release_pending(slots);
    twlhd_geometry_slots_release_active(slots);
}
