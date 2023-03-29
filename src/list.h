// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Intrusive linked lists.
 */

#ifndef BFS_LIST_H
#define BFS_LIST_H

#include "config.h"
#include <stdbool.h>

/**
 * A singly-linked list entry.
 */
struct slink {
	struct slink *next;
};

/** Initialize a list entry. */
void slink_init(struct slink *link);

/**
 * A singly-linked list.
 */
struct slist {
	struct slink *head;
	struct slink **tail;
};

/** Initialize an empty list. */
void slist_init(struct slist *list);

/** Check if a list is empty. */
bool slist_is_empty(const struct slist *list);

/** Add an entry at the tail of the list. */
void slist_append(struct slist *list, struct slink *link);

/** Add an entry at the head of the list. */
void slist_prepend(struct slist *list, struct slink *link);

/** Add an entire list at the tail of the list. */
void slist_extend(struct slist *dest, struct slist *src);

/** Remove the head of the list. */
struct slink *slist_pop(struct slist *list);

/**
 * Comparison function type for slist_sort().
 *
 * @param left
 *         The left-hand side of the comparison.
 * @param right
 *         The right-hand side of the comparison.
 * @param ptr
 *         An arbitrary pointer passed to slist_sort().
 * @return
 *         Whether left <= right.
 */
typedef bool slist_cmp_fn(struct slink *left, struct slink *right, const void *ptr);

/** Sort a list. */
void slist_sort(struct slist *list, slist_cmp_fn *cmp_fn, const void *ptr);

/**
 * A doubly-linked list entry.
 */
struct link {
	struct link *prev;
	struct link *next;
};

/** Initialize a list entry. */
void link_init(struct link *link);

/**
 * A doubly-linked list.
 */
struct list {
	struct link *head;
	struct link *tail;
};

/** Initialize an empty list. */
void list_init(struct list *list);

/** Check if a list is empty. */
bool list_is_empty(const struct list *list);

/** Add an entry at the tail of the list. */
void list_append(struct list *list, struct link *link);

/** Add an entry at the head of the list. */
void list_prepend(struct list *list, struct link *link);

/** Insert an entry after the target entry. */
void list_insert_after(struct list *list, struct link *target, struct link *link);

/** Remove an entry from a list. */
void list_remove(struct list *list, struct link *link);

/** Remove the head of the list. */
struct link *list_pop(struct list *list);

/** Check if a link is attached to a list. */
bool list_attached(const struct list *list, const struct link *link);

// LIST_ITEM() helper
#define LIST_ITEM_IMPL(type, entry, member, ...) \
	BFS_CONTAINER_OF(entry, type, member)

/**
 * Convert a list entry to its container.
 *
 * @param type
 *         The type of the list entries.
 * @param entry
 *         The list entry to convert.
 * @param member
 *         The name of the list link field (default: link).
 * @return
 *         The item that contains the given entry.
 */
#define LIST_ITEM(...) \
	LIST_ITEM_IMPL(__VA_ARGS__, link,)

// LIST_NEXT() helper
#define LIST_NEXT_IMPL(type, entry, member, ...) \
	LIST_ITEM(type, (entry)->member.next, member)

/**
 * Get the next item in a list.
 *
 * @param type
 *         The type of the list entries.
 * @param entry
 *         The current entry.
 * @param member
 *         The name of the list link field (default: link).
 * @return
 *         The next item in the list.
 */
#define LIST_NEXT(...) \
	LIST_NEXT_IMPL(__VA_ARGS__, link,)

// LIST_PREV() helper
#define LIST_PREV_IMPL(type, entry, member, ...) \
	LIST_ITEM(type, (entry)->member.prev, member)

/**
 * Get the previous entry in a list.
 *
 * @param type
 *         The type of the list entries.
 * @param entry
 *         The current entry.
 * @param member
 *         The name of the list link field (default: link).
 * @return
 *         The previous item in the list.
 */
#define LIST_PREV(...) \
	LIST_PREV_IMPL(__VA_ARGS__, link,)

// Helper for LIST_FOR_EACH_*()
#define LIST_FOR_EACH_IMPL(entry, type, i, member, ...) \
	for (type *_next, *i = LIST_ITEM(type, entry, member);	\
	     i && (_next = LIST_NEXT(type, i, member), true); \
	     i = _next)

/**
 * Iterate over a list from the given entry.
 *
 * @param entry
 *         The entry to start from.
 * @param type
 *         The type of the list entries.
 * @param i
 *         The name of the loop variable, declared as type *i.
 * @param member
 *         The name of the list link field (default: link).
 */
#define LIST_FOR_EACH_FROM(...) \
	LIST_FOR_EACH_IMPL(__VA_ARGS__, link,)

/**
 * Iterate over a list.
 *
 * @param list
 *         The list to iterate over.
 * @param type
 *         The type of the list entries.
 * @param i
 *         The name of the loop variable, declared as type *i.
 * @param member
 *         The name of the list link field (default: link).
 */
#define LIST_FOR_EACH(list, ...) \
	LIST_FOR_EACH_FROM((list)->head, __VA_ARGS__)

// Pop from a list or slist
#define LIST_POP(l) _Generic((l), \
	struct list *: list_pop((struct list *)l), \
	struct slist *: slist_pop((struct slist *)l))

// Helper for LIST_DRAIN()
#define LIST_DRAIN_IMPL(list, type, i, member, ...) \
	for (type *i; (i = LIST_ITEM(type, LIST_POP(list), member));)

/**
 * Drain the entries from a list.
 *
 * @param list
 *         The list to drain.
 * @param type
 *         The type of the list entries.
 * @param i
 *         The name of the loop variable, declared as type *i.
 * @param member
 *         The name of the list link field (default: link).
 */
#define LIST_DRAIN(...) \
	LIST_DRAIN_IMPL(__VA_ARGS__, link,)

#endif // BFS_LIST_H
