// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B/4C repair - active/pending vertex-buffer state machine.
 *
 * Pure C11 helper that owns a pair of geometry buffers: one that may be
 * consumed by the currently in-flight GPU frame (active), and one that has
 * been prepared for the next frame (pending). Allocation and freeing are
 * injected so the ARM11 renderer can use linearAlloc/linearFree while host
 * tests use counting mocks.
 *
 * Lifecycle rules:
 *   - stage() allocates a private copy of the caller's data and frees only a
 *     prior pending buffer. It never frees the active buffer.
 *   - commit() runs after the GPU has finished reading the active buffer; it
 *     frees the active buffer and promotes pending -> active.
 *   - release_pending() and release_active() let the caller drain the GPU
 *     queue between freeing the two buffers.
 *
 * This module has no libctru/libnds dependencies.
 */
#pragma once

#include <stddef.h>

typedef void *(*TwlhdGeometryAllocFn)(size_t size);
typedef void (*TwlhdGeometryFreeFn)(void *ptr);

typedef struct {
    void *active;
    size_t active_size;
    void *pending;
    size_t pending_size;
    TwlhdGeometryAllocFn alloc;
    TwlhdGeometryFreeFn free;
} TwlhdGeometrySlots;

/* Initialize a slot pair. `alloc_fn` and `free_fn` must remain valid for the
 * lifetime of `slots`. */
void twlhd_geometry_slots_init(TwlhdGeometrySlots *slots,
                               TwlhdGeometryAllocFn alloc_fn,
                               TwlhdGeometryFreeFn free_fn);

/* Allocate and store a private copy of `data` (size `size` bytes) as the
 * pending buffer. Any previous pending buffer is freed immediately. The active
 * buffer is never touched. A zero-length stage (`data` may be NULL, `size` 0)
 * clears the pending buffer.
 *
 * Returns 0 on success, -1 if allocation fails. On failure the existing active
 * and pending buffers are left untouched. */
int twlhd_geometry_slots_stage(TwlhdGeometrySlots *slots,
                               const void *data, size_t size);

/* Promote pending -> active. The previous active buffer is freed. If no
 * pending buffer exists this is a no-op. Safe to call repeatedly. */
void twlhd_geometry_slots_commit(TwlhdGeometrySlots *slots);

/* Free the pending buffer immediately. Idempotent. */
void twlhd_geometry_slots_release_pending(TwlhdGeometrySlots *slots);

/* Free the active buffer. The caller must guarantee the GPU queue has drained
 * before calling this. Idempotent. */
void twlhd_geometry_slots_release_active(TwlhdGeometrySlots *slots);

/* Free both buffers. Only use when the caller does not need to wait for the
 * queue between the two frees; otherwise call release_pending() and
 * release_active() separately. */
void twlhd_geometry_slots_release_all(TwlhdGeometrySlots *slots);
