// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Intrusive linked lists.
 *
 * Singly-linked lists are declared like this:
 *
 *     struct item {
 *             struct item *next;
 *     };
 *
 *     struct list {
 *             struct item *head;
 *             struct item **tail;
 *     };
 *
 * The SLIST_*() macros manipulate singly-linked lists.
 *
 *     struct list list;
 *     SLIST_INIT(&list);
 *
 *     struct item item;
 *     SLIST_ITEM_INIT(&item);
 *     SLIST_APPEND(&list, &item);
 *
 * Doubly linked lists are similar:
 *
 *     struct item {
 *             struct item *next;
 *             struct item *prev;
 *     };
 *
 *     struct list {
 *             struct item *head;
 *             struct item *tail;
 *     };
 *
 *     struct list list;
 *     LIST_INIT(&list);
 *
 *     struct item item;
 *     LIST_ITEM_INIT(&item);
 *     LIST_APPEND(&list, &item);
 *
 * Items can be on multiple lists at once:
 *
 *     struct item {
 *             struct {
 *                     struct item *next;
 *             } chain;
 *
 *             struct {
 *                     struct item *next;
 *                     struct item *prev;
 *             } lru;
 *     };
 *
 *     struct items {
 *             struct {
 *                     struct item *head;
 *                     struct item **tail;
 *             } queue;
 *
 *             struct {
 *                     struct item *head;
 *                     struct item *tail;
 *             } cache;
 *     };
 *
 *     struct items items;
 *     SLIST_INIT(&items.queue);
 *     LIST_INIT(&items.cache);
 *
 *     struct item item;
 *     SLIST_ITEM_INIT(&item, chain);
 *     SLIST_APPEND(&items.queue, &item, chain);
 *     LIST_ITEM_INIT(&item, lru);
 *     LIST_APPEND(&items.cache, &item, lru);
 */

#ifndef BFS_LIST_H
#define BFS_LIST_H

#include "bfs.h"
#include "diag.h"

#include <stddef.h>
#include <string.h>

/**
 * Initialize a singly-linked list.
 *
 * @list
 *         The list to initialize.
 *
 * ---
 *
 * Like many macros in this file, this macro delegates the bulk of its work to
 * some helper macros.  We explicitly parenthesize (list) here so the helpers
 * don't have to.
 */
#define SLIST_INIT(list) \
	SLIST_INIT_((list))

/**
 * Helper for SLIST_INIT().
 */
#define SLIST_INIT_(list) LIST_VOID_( \
	list->head = NULL, \
	list->tail = &list->head)

/**
 * Cast a list of expressions to void.
 */
#define LIST_VOID_(...) ((void)(__VA_ARGS__))

/**
 * Initialize a singly-linked list item.
 *
 * @item
 *         The item to initialize.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define SLIST_ITEM_INIT(item, ...) \
	SLIST_ITEM_INIT_((item), LIST_NEXT_(__VA_ARGS__))

#define SLIST_ITEM_INIT_(item, next) \
	LIST_VOID_(item->next = NULL)

/**
 * Get the projection for an item's next pointer.
 *
 *     LIST_NEXT_()     => next
 *     LIST_NEXT_(node) => node.next
 */
#define LIST_NEXT_(node) \
	BFS_VA_IF(node)(node.next)(next)

/**
 * Type-checking macro for singly-linked lists.
 */
#define SLIST_CHECK_(list) \
	(void)sizeof(list->tail - &list->head)

/**
 * Get the head of a singly-linked list.
 *
 * @list
 *         The list in question.
 * @return
 *         The first item in the list.
 */
#define SLIST_HEAD(list) \
	SLIST_HEAD_((list))

#define SLIST_HEAD_(list) \
	(SLIST_CHECK_(list), list->head)

/**
 * Check if a singly-linked list is empty.
 */
#define SLIST_EMPTY(list) \
	(!SLIST_HEAD(list))

/**
 * Get the tail of a singly-linked list.
 *
 * @list
 *         The list in question.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 * @return
 *         The last item in the list.
 */
#define SLIST_TAIL(list, ...) \
	SLIST_TAIL_((list), LIST_NEXT_(__VA_ARGS__))

#define SLIST_TAIL_(list, next) \
	(list->head ? container_of(list->tail, typeof(*list->head), next) : NULL)

/**
 * Check if an item is attached to a singly-linked list.
 *
 * @list
 *         The list to check.
 * @item
 *         The item to check.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 * @return
 *         Whether the item is attached to the list.
 */
#define SLIST_ATTACHED(list, item, ...) \
	SLIST_ATTACHED_((list), (item), LIST_NEXT_(__VA_ARGS__))

#define SLIST_ATTACHED_(list, item, next) \
	(item->next || list->tail == &item->next)

/**
 * Insert an item into a singly-linked list.
 *
 * @list
 *         The list to modify.
 * @cursor
 *         A pointer to the item to insert after, e.g. &list->head or list->tail.
 * @item
 *         The item to insert.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 * @return
 *         A cursor for the next item.
 */
#define SLIST_INSERT(list, cursor, item, ...) \
	SLIST_INSERT_((list), (cursor), (item), LIST_NEXT_(__VA_ARGS__))

#define SLIST_INSERT_(list, cursor, item, next) \
	(bfs_assert(!SLIST_ATTACHED_(list, item, next)), \
	 item->next = *cursor, \
	 *cursor = item, \
	 list->tail = item->next ? list->tail : &item->next, \
	 &item->next)

/**
 * Add an item to the tail of a singly-linked list.
 *
 * @list
 *         The list to modify.
 * @item
 *         The item to append.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define SLIST_APPEND(list, item, ...) \
	LIST_VOID_(SLIST_INSERT(list, (list)->tail, item, __VA_ARGS__))

/**
 * Add an item to the head of a singly-linked list.
 *
 * @list
 *         The list to modify.
 * @item
 *         The item to prepend.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define SLIST_PREPEND(list, item, ...) \
	LIST_VOID_(SLIST_INSERT(list, &(list)->head, item, __VA_ARGS__))

/**
 * Splice a singly-linked list into another.
 *
 * @dest
 *         The destination list.
 * @cursor
 *         A pointer to the item to splice after, e.g. &list->head or list->tail.
 * @src
 *         The source list.
 */
#define SLIST_SPLICE(dest, cursor, src) \
	LIST_VOID_(SLIST_SPLICE_((dest), (cursor), (src)))

#define SLIST_SPLICE_(dest, cursor, src) \
	*src->tail = *cursor, \
	*cursor = src->head, \
	dest->tail = *dest->tail ? src->tail : dest->tail, \
	SLIST_INIT(src)

/**
 * Add an entire singly-linked list to the tail of another.
 *
 * @dest
 *         The destination list.
 * @src
 *         The source list.
 */
#define SLIST_EXTEND(dest, src) \
	SLIST_SPLICE(dest, (dest)->tail, src)

/**
 * Remove an item from a singly-linked list.
 *
 * @list
 *         The list to modify.
 * @cursor
 *         A pointer to the item to remove, either &list->head or &prev->next.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 * @return
 *         The removed item.
 */
#define SLIST_REMOVE(list, cursor, ...) \
	SLIST_REMOVE_((list), (cursor), LIST_NEXT_(__VA_ARGS__))

#define SLIST_REMOVE_(list, cursor, next) \
	(list->tail = (*cursor)->next ? list->tail : cursor, \
	 (typeof(*cursor))slist_remove_(*cursor, cursor, &(*cursor)->next, sizeof(*cursor)))

// Helper for SLIST_REMOVE()
static inline void *slist_remove_(void *ret, void *cursor, void *next, size_t size) {
	// ret = *cursor;
	// *cursor = ret->next;
	memcpy(cursor, next, size);
	// ret->next = NULL;
	memset(next, 0, size);
	return ret;
}

/**
 * Pop the head off a singly-linked list.
 *
 * @list
 *         The list to modify.
 * @node (optional)
 *         If specified, use head->node.next rather than head->next.
 * @return
 *         The popped item, or NULL if the list was empty.
 */
#define SLIST_POP(list, ...) \
	((list)->head ? SLIST_REMOVE(list, &(list)->head, __VA_ARGS__) : NULL)

/**
 * Loop over the items in a singly-linked list.
 *
 * @type
 *         The list item type.
 * @item
 *         The induction variable name.
 * @list
 *         The list to iterate.
 * @node (optional)
 *         If specified, use head->node.next rather than head->next.
 */
#define for_slist(type, item, list, ...) \
	for_slist_(type, item, (list), LIST_NEXT_(__VA_ARGS__))

#define for_slist_(type, item, list, next) \
	for (type *item = SLIST_HEAD(list), *_next; \
	     item && (_next = item->next, true); \
	     item = _next)

/**
 * Loop over a singly-linked list, popping each item.
 *
 * @type
 *         The list item type.
 * @item
 *         The induction variable name.
 * @list
 *         The list to drain.
 * @node (optional)
 *         If specified, use head->node.next rather than head->next.
 */
#define drain_slist(type, item, list, ...) \
	for (type *item; (item = SLIST_POP(list, __VA_ARGS__));)

/**
 * Initialize a doubly-linked list.
 *
 * @list
 *         The list to initialize.
 */
#define LIST_INIT(list) \
	LIST_INIT_((list))

#define LIST_INIT_(list) \
	LIST_VOID_(list->head = list->tail = NULL)

/**
 * LIST_PREV_()     => prev
 * LIST_PREV_(node) => node.prev
 */
#define LIST_PREV_(node) \
	BFS_VA_IF(node)(node.prev)(prev)

/**
 * Initialize a doubly-linked list item.
 *
 * @item
 *         The item to initialize.
 * @node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define LIST_ITEM_INIT(item, ...) \
	LIST_ITEM_INIT_((item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_ITEM_INIT_(item, prev, next) \
	LIST_VOID_(item->prev = item->next = NULL)

/**
 * Type-checking macro for doubly-linked lists.
 */
#define LIST_CHECK_(list) \
	(void)sizeof(list->tail - list->head)

/**
 * Check if a doubly-linked list is empty.
 */
#define LIST_EMPTY(list) \
	(LIST_CHECK_(list), !(list)->head)

/**
 * Add an item to the tail of a doubly-linked list.
 *
 * @list
 *         The list to modify.
 * @item
 *         The item to append.
 * @node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_APPEND(list, item, ...) \
	LIST_INSERT(list, (list)->tail, item, __VA_ARGS__)

/**
 * Add an item to the head of a doubly-linked list.
 *
 * @list
 *         The list to modify.
 * @item
 *         The item to prepend.
 * @node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_PREPEND(list, item, ...) \
	LIST_INSERT(list, NULL, item, __VA_ARGS__)

/**
 * Check if an item is attached to a doubly-linked list.
 *
 * @list
 *         The list to check.
 * @item
 *         The item to check.
 * @node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 * @return
 *         Whether the item is attached to the list.
 */
#define LIST_ATTACHED(list, item, ...) \
	LIST_ATTACHED_((list), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_ATTACHED_(list, item, prev, next) \
	(item->prev || item->next || list->head == item || list->tail == item)

/**
 * Insert into a doubly-linked list after the given cursor.
 *
 * @list
 *         The list to modify.
 * @cursor
 *         Insert after this element.
 * @item
 *         The item to insert.
 * @node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_INSERT(list, cursor, item, ...) \
	LIST_INSERT_((list), (cursor), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_INSERT_(list, cursor, item, prev, next) LIST_VOID_( \
	bfs_assert(!LIST_ATTACHED_(list, item, prev, next)), \
	item->prev = cursor, \
	item->next = cursor ? cursor->next : list->head, \
	*(item->prev ? &item->prev->next : &list->head) = item, \
	*(item->next ? &item->next->prev : &list->tail) = item)

/**
 * Remove an item from a doubly-linked list.
 *
 * @list
 *         The list to modify.
 * @item
 *         The item to remove.
 * @node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_REMOVE(list, item, ...) \
	LIST_REMOVE_((list), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_REMOVE_(list, item, prev, next) LIST_VOID_( \
	*(item->prev ? &item->prev->next : &list->head) = item->next, \
	*(item->next ? &item->next->prev : &list->tail) = item->prev, \
	item->prev = item->next = NULL)

/**
 * Loop over the items in a doubly-linked list.
 *
 * @type
 *         The list item type.
 * @item
 *         The induction variable name.
 * @list
 *         The list to iterate.
 * @node (optional)
 *         If specified, use head->node.next rather than head->next.
 */
#define for_list(type, item, list, ...) \
	for_list_(type, item, (list), LIST_NEXT_(__VA_ARGS__))

#define for_list_(type, item, list, next) \
	for (type *item = (LIST_CHECK_(list), list->head), *_next; \
	     item && (_next = item->next, true); \
	     item = _next)

#endif // BFS_LIST_H
