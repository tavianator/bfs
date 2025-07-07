// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "bfs.h"
#include "diag.h"
#include "list.h"

#include <stddef.h>

struct item {
	int n;
	struct item *next;
};

struct list {
	struct item *head;
	struct item **tail;
};

static bool check_list_items(struct list *list, int *array, size_t size) {
	struct item **cur = &list->head;
	for (size_t i = 0; i < size; ++i) {
		if (!bfs_check(*cur != NULL)) {
			return false;
		}
		int n = (*cur)->n;
		if (!bfs_check(n == array[i], "%d != %d", n, array[i])) {
			return false;
		}
		cur = &(*cur)->next;
	}

	if (!bfs_check(*cur == NULL)) {
		return false;
	}
	if (!bfs_check(list->tail == cur)) {
		return false;
	}

	return true;
}

#define ARRAY(...) (int[]){ __VA_ARGS__ }, countof((int[]){ __VA_ARGS__ })
#define EMPTY() NULL, 0

void check_list(void) {
	struct list l1;
	SLIST_INIT(&l1);
	bfs_verify(check_list_items(&l1, EMPTY()));

	struct list l2;
	SLIST_INIT(&l2);
	bfs_verify(check_list_items(&l2, EMPTY()));

	SLIST_EXTEND(&l1, &l2);
	bfs_verify(check_list_items(&l1, EMPTY()));

	struct item i10 = { .n = 10 };
	SLIST_APPEND(&l1, &i10);
	bfs_verify(check_list_items(&l1, ARRAY(10)));

	SLIST_EXTEND(&l1, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10)));

	SLIST_SPLICE(&l1, &l1.head, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10)));

	struct item i20 = { .n = 20 };
	SLIST_PREPEND(&l2, &i20);
	bfs_verify(check_list_items(&l2, ARRAY(20)));

	SLIST_EXTEND(&l1, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10, 20)));
	bfs_verify(check_list_items(&l2, EMPTY()));

	struct item i15 = { .n = 15 };
	SLIST_APPEND(&l2, &i15);
	SLIST_SPLICE(&l1, &i10.next, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10, 15, 20)));
	bfs_verify(check_list_items(&l2, EMPTY()));

	SLIST_EXTEND(&l1, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10, 15, 20)));

	SLIST_SPLICE(&l1, &i10.next, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10, 15, 20)));

	SLIST_SPLICE(&l1, &l1.head, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10, 15, 20)));

	struct item i11 = { .n = 11 };
	struct item i12 = { .n = 12 };
	SLIST_APPEND(&l2, &i11);
	SLIST_APPEND(&l2, &i12);
	SLIST_SPLICE(&l1, &l1.head->next, &l2);
	bfs_verify(check_list_items(&l1, ARRAY(10, 11, 12, 15, 20)));

	// Check the return type of SLIST_POP()
	bfs_check(SLIST_POP(&l1)->n == 10);
}
