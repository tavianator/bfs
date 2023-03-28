// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "list.h"
#include <assert.h>
#include <stddef.h>

void slink_init(struct slink *link) {
	link->next = NULL;
}

void slist_init(struct slist *list) {
	list->head = NULL;
	list->tail = &list->head;
}

bool slist_is_empty(const struct slist *list) {
	return !list->head;
}

void slist_append(struct slist *list, struct slink *link) {
	assert(!link->next);
	*list->tail = link;
	list->tail = &link->next;
}

void slist_prepend(struct slist *list, struct slink *link) {
	assert(!link->next);
	if (!list->head) {
		list->tail = &link->next;
	}
	link->next = list->head;
	list->head = link;
}

void slist_extend(struct slist *dest, struct slist *src) {
	if (src->head) {
		*dest->tail = src->head;
		dest->tail = src->tail;
		slist_init(src);
	}
}

struct slink *slist_pop(struct slist *list) {
	struct slink *head = list->head;
	if (!head) {
		return NULL;
	}

	list->head = head->next;
	if (!list->head) {
		list->tail = &list->head;
	}

	head->next = NULL;
	return head;
}

void slist_sort(struct slist *list, slist_cmp_fn *cmp_fn, const void *ptr) {
	if (!list->head || !list->head->next) {
		return;
	}

	struct slist left, right;
	slist_init(&left);
	slist_init(&right);

	// Split
	for (struct slink *hare = list->head; hare && (hare = hare->next); hare = hare->next) {
		slist_append(&left, slist_pop(list));
	}
	slist_extend(&right, list);

	// Recurse
	slist_sort(&left, cmp_fn, ptr);
	slist_sort(&right, cmp_fn, ptr);

	// Merge
	while (left.head && right.head) {
		if (cmp_fn(left.head, right.head, ptr)) {
			slist_append(list, slist_pop(&left));
		} else {
			slist_append(list, slist_pop(&right));
		}
	}
	slist_extend(list, &left);
	slist_extend(list, &right);
}

void link_init(struct link *link) {
	link->prev = NULL;
	link->next = NULL;
}

void list_init(struct list *list) {
	list->head = NULL;
	list->tail = NULL;
}

bool list_is_empty(const struct list *list) {
	return !list->head;
}

void list_append(struct list *list, struct link *link) {
	list_insert_after(list, list->tail, link);
}

void list_prepend(struct list *list, struct link *link) {
	list_insert_after(list, NULL, link);
}

void list_insert_after(struct list *list, struct link *target, struct link *link) {
	assert(!list_attached(list, link));

	if (target) {
		link->prev = target;
		link->next = target->next;
	} else {
		link->next = list->head;
	}

	if (link->prev) {
		link->prev->next = link;
	} else {
		list->head = link;
	}

	if (link->next) {
		link->next->prev = link;
	} else {
		list->tail = link;
	}
}

void list_remove(struct list *list, struct link *link) {
	if (link->prev) {
		assert(list->head != link);
		link->prev->next = link->next;
	} else {
		assert(list->head == link);
		list->head = link->next;
	}

	if (link->next) {
		assert(list->tail != link);
		link->next->prev = link->prev;
	} else {
		assert(list->tail == link);
		list->tail = link->prev;
	}

	link->prev = NULL;
	link->next = NULL;
}

struct link *list_pop(struct list *list) {
	struct link *head = list->head;
	if (!head) {
		return NULL;
	}

	list_remove(list, head);
	return head;
}

bool list_attached(const struct list *list, const struct link *link) {
	return link->prev || list->head == link
		|| link->next || list->tail == link;
}
