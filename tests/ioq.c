// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "tests.h"
#include "ioq.h"
#include "bfstd.h"
#include "diag.h"
#include "dir.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

/**
 * Test for blocking within ioq_slot_push().
 *
 * struct ioqq only supports non-blocking reads; if a write encounters a full
 * slot, it must block until someone pops from that slot:
 *
 *     Reader                        Writer
 *     ──────────────────────────    ─────────────────────────
 *                                   tail:         0 → 1
 *                                   slots[0]: empty → full
 *                                   tail:         1 → 0
 *                                   slots[1]: empty → full
 *                                   tail:         0 → 1
 *                                   slots[0]:  full → full*    (IOQ_BLOCKED)
 *                                   ioq_slot_wait() ...
 *     head:         0 → 1
 *     slots[0]: full* → empty
 *     ioq_slot_wake()
 *                                   ...
 *                                   slots[0]: empty → full
 *
 * To reproduce this unlikely scenario, we must fill up the ready queue, then
 * call ioq_cancel() which pushes an additional sentinel IOQ_STOP operation.
 */
static void check_ioq_push_block(void) {
	// Must be a power of two to fill the entire queue
	const size_t depth = 2;

	struct ioq *ioq = ioq_create(depth, 1);
	bfs_everify(ioq, "ioq_create()");

	// Push enough operations to fill the queue
	for (size_t i = 0; i < depth; ++i) {
		struct bfs_dir *dir = bfs_allocdir();
		bfs_everify(dir, "bfs_allocdir()");

		int ret = ioq_opendir(ioq, dir, AT_FDCWD, ".", 0, NULL);
		bfs_everify(ret == 0, "ioq_opendir()");
	}
	bfs_verify(ioq_capacity(ioq) == 0);

	// Now cancel the queue, pushing an additional IOQ_STOP message
	ioq_cancel(ioq);

	// Drain the queue
	for (size_t i = 0; i < depth; ++i) {
		struct ioq_ent *ent = ioq_pop(ioq, true);
		bfs_verify(ent && ent->op == IOQ_OPENDIR);

		if (ent->result >= 0) {
			bfs_closedir(ent->opendir.dir);
		}
		free(ent->opendir.dir);
		ioq_free(ioq, ent);
	}
	bfs_verify(!ioq_pop(ioq, true));

	ioq_destroy(ioq);
}

void check_ioq(void) {
	check_ioq_push_block();
}
