// SPDX-License-Identifier: Apache-2.0
/*
 * Host test for the active/pending vertex-buffer state machine.
 *
 * Verifies that geometry_slots never frees the active buffer during stage(),
 * frees the old active exactly once during commit(), and supports the
 * ordered shutdown sequence (release pending immediately, release active only
 * after the GPU queue is known to be drained).
 */

#include "twlhd_geometry_slots.h"

#include "tests_host.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ALLOCS 64

typedef struct {
    int id;
    int freed;
    int allocated;
} AllocRecord;

static AllocRecord g_records[MAX_ALLOCS];
static int g_next_id;
static int g_alloc_count;
static int g_total_freed;
static int g_fail_after; /* -1 = never fail */

static void test_alloc_reset(void) {
    memset(g_records, 0, sizeof g_records);
    g_next_id = 1;
    g_alloc_count = 0;
    g_total_freed = 0;
    g_fail_after = -1;
}

static int id_of(const void *ptr) {
    const int *id_ptr = (const int *)((const char *)ptr - sizeof(int));
    return *id_ptr;
}

static AllocRecord *record_of(const void *ptr) {
    return &g_records[id_of(ptr) - 1];
}

static void *test_alloc(size_t size) {
    AllocRecord *rec;
    int id;
    void *block;

    g_alloc_count++;
    if (g_fail_after >= 0 && g_alloc_count > g_fail_after) return NULL;

    id = g_next_id++;
    rec = &g_records[id - 1];
    rec->id = id;
    rec->freed = 0;
    rec->allocated = 1;

    block = malloc(sizeof(int) + size);
    *(int *)block = id;
    return (char *)block + sizeof(int);
}

static void test_free(void *ptr) {
    AllocRecord *rec;
    int id;

    if (!ptr) return;
    id = id_of(ptr);
    rec = &g_records[id - 1];
    rec->freed++;
    g_total_freed++;
    free((char *)ptr - sizeof(int));
}

static void init_slots(TwlhdGeometrySlots *slots) {
    twlhd_geometry_slots_init(slots, test_alloc, test_free);
}

int main(void) {
    TwlhdGeometrySlots slots;
    const char src1[] = "source-one";
    const char src2[] = "source-two";
    const char src3[] = "source-three";

    test_alloc_reset();
    init_slots(&slots);

    /* Stage frees only a prior pending buffer, never the active one. */
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    CHECK(slots.active == NULL);
    CHECK(slots.active_size == 0);
    CHECK(slots.pending != NULL);
    CHECK(record_of(slots.pending)->freed == 0);

    CHECK(twlhd_geometry_slots_stage(&slots, src2, sizeof(src2)) == 0);
    CHECK(slots.active == NULL);
    CHECK(record_of(slots.pending)->freed == 0);

    /* Commit frees the old active and promotes pending. */
    twlhd_geometry_slots_commit(&slots);
    CHECK(slots.active != NULL);
    CHECK(slots.pending == NULL);
    CHECK(slots.pending_size == 0);
    CHECK(record_of(slots.active)->freed == 0);

    /* Stage with an active buffer: active is untouched. */
    {
        void *active_before = slots.active;
        CHECK(twlhd_geometry_slots_stage(&slots, src3, sizeof(src3)) == 0);
        CHECK(slots.active == active_before);
        CHECK(record_of(slots.active)->freed == 0);
    }

    /* Second commit frees the previous active exactly once and promotes. */
    {
        AllocRecord *old_active = record_of(slots.active);
        twlhd_geometry_slots_commit(&slots);
        CHECK(old_active->freed == 1);
        CHECK(slots.pending == NULL);
        CHECK(record_of(slots.active)->freed == 0);
    }

    /* Commit with no pending is a no-op. */
    {
        void *active_after = slots.active;
        twlhd_geometry_slots_commit(&slots);
        CHECK(slots.active == active_after);
        CHECK(record_of(slots.active)->freed == 0);
    }

    twlhd_geometry_slots_release_all(&slots);
    test_report("geometry_slots/stage-commit-active-safety");

    /* Release is idempotent and frees exactly once. */
    init_slots(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    {
        AllocRecord *r = record_of(slots.pending);
        twlhd_geometry_slots_release_pending(&slots);
        CHECK(r->freed == 1);
        CHECK(slots.pending == NULL);
        twlhd_geometry_slots_release_pending(&slots);
        CHECK(r->freed == 1);
    }

    init_slots(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    twlhd_geometry_slots_commit(&slots);
    {
        AllocRecord *r = record_of(slots.active);
        twlhd_geometry_slots_release_active(&slots);
        CHECK(r->freed == 1);
        CHECK(slots.active == NULL);
        twlhd_geometry_slots_release_active(&slots);
        CHECK(r->freed == 1);
    }

    /* release_all frees both buffers and is idempotent. */
    init_slots(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    twlhd_geometry_slots_commit(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src2, sizeof(src2)) == 0);
    {
        AllocRecord *r_active = record_of(slots.active);
        AllocRecord *r_pending = record_of(slots.pending);
        twlhd_geometry_slots_release_all(&slots);
        CHECK(r_active->freed == 1);
        CHECK(r_pending->freed == 1);
        CHECK(slots.active == NULL);
        CHECK(slots.pending == NULL);
        twlhd_geometry_slots_release_all(&slots);
        CHECK(r_active->freed == 1);
        CHECK(r_pending->freed == 1);
    }

    test_report("geometry_slots/release-idempotent");

    /* Allocation failure leaves active and pending untouched. */
    init_slots(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    {
        void *pending_before = slots.pending;
        AllocRecord *r = record_of(pending_before);
        g_fail_after = 0; /* next alloc fails */
        CHECK(twlhd_geometry_slots_stage(&slots, src2, sizeof(src2)) == -1);
        CHECK(slots.active == NULL);
        CHECK(slots.pending == pending_before);
        CHECK(r->freed == 0);
        g_fail_after = -1;
    }
    twlhd_geometry_slots_release_all(&slots);

    test_report("geometry_slots/alloc-failure-preserves-state");

    /* Zero-length stage clears the pending buffer. */
    init_slots(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    {
        AllocRecord *r = record_of(slots.pending);
        CHECK(twlhd_geometry_slots_stage(&slots, NULL, 0) == 0);
        CHECK(r->freed == 1);
        CHECK(slots.pending == NULL);
        CHECK(slots.pending_size == 0);
    }
    twlhd_geometry_slots_release_all(&slots);

    test_report("geometry_slots/zero-length-stage-clears");

    /* Data is copied into the staged buffer. */
    init_slots(&slots);
    CHECK(twlhd_geometry_slots_stage(&slots, src1, sizeof(src1)) == 0);
    CHECK(memcmp(slots.pending, src1, sizeof(src1)) == 0);
    twlhd_geometry_slots_commit(&slots);
    CHECK(memcmp(slots.active, src1, sizeof(src1)) == 0);
    twlhd_geometry_slots_release_all(&slots);

    test_report("geometry_slots/data-copied");

    /* Every successful allocation was freed exactly once. ASan independently
     * checks for leaks and double frees. */
    CHECK_EQ_INT(g_total_freed, g_next_id - 1);

    test_report("geometry_slots/allocation-counter-balance");

    return test_exit_code();
}
