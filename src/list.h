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

#include "diag.h"
#include <stddef.h>
#include <string.h>

/**
 * Initialize a singly-linked list.
 *
 * @param list
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
 * @param item
 *         The item to initialize.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 *
 * ---
 *
 * We play some tricks with variadic macros to handle the optional parameter:
 *
 *     SLIST_ITEM_INIT(item)       => item->next = NULL
 *     SLIST_ITEM_INIT(item, node) => item->node.next = NULL
 *
 * The first trick is that
 *
 *     #define SLIST_ITEM_INIT(item, ...)
 *
 * won't work because both commas are required (until C23; see N3033). As a
 * workaround, we dispatch to another macro and add a trailing comma.
 *
 *     SLIST_ITEM_INIT(item)       => SLIST_ITEM_INIT_(item, )
 *     SLIST_ITEM_INIT(item, node) => SLIST_ITEM_INIT_(item, node, )
 */
#define SLIST_ITEM_INIT(...) \
	SLIST_ITEM_INIT_(__VA_ARGS__, )

/**
 * Now we need a way to generate either ->next or ->node.next depending on
 * whether the node parameter was passed.  The approach is based on
 *
 *     #define FOO(...) BAR(__VA_ARGS__, 1, 2, )
 *     #define BAR(x, y, z, ...) z
 *
 *     FOO(a)    => 2
 *     FOO(a, b) => 1
 *
 * The LIST_NEXT_() macro uses this technique:
 *
 *     LIST_NEXT_()       => LIST_NODE_(next, )
 *     LIST_NEXT_(node, ) => LIST_NODE_(next, node, )
 */
#define LIST_NEXT_(...) \
	LIST_NODE_(next, __VA_ARGS__)

/**
 * LIST_NODE_() dispatches to yet another macro:
 *
 *     LIST_NODE_(next, )       => LIST_NODE__(next,     , . ,   , )
 *     LIST_NODE_(next, node, ) => LIST_NODE__(next, node,   , . , , )
 */
#define LIST_NODE_(dir, ...) \
	LIST_NODE__(dir, __VA_ARGS__, . , , )

/**
 * And finally, LIST_NODE__() adds the node and the dot if necessary.
 *
 *                 dir   node ignored dot
 *                  v     v      v     v
 *     LIST_NODE__(next,     ,   .   ,   , )   => next
 *     LIST_NODE__(next, node,       , . , , ) => node . next
 *                  ^     ^      ^     ^
 *                 dir   node ignored dot
 */
#define LIST_NODE__(dir, node, ignored, dot, ...) \
	node dot dir

/**
 * SLIST_ITEM_INIT_() uses LIST_NEXT_() to generate the right name for the list
 * node, and finally delegates to the actual implementation.
 */
#define SLIST_ITEM_INIT_(item, ...) \
	SLIST_ITEM_INIT__((item), LIST_NEXT_(__VA_ARGS__))

#define SLIST_ITEM_INIT__(item, next) \
	LIST_VOID_(item->next = NULL)

/**
 * Type-checking macro for singly-linked lists.
 */
#define SLIST_CHECK_(list) \
	(void)sizeof(list->tail - &list->head)

/**
 * Check if a singly-linked list is empty.
 */
#define SLIST_EMPTY(list) \
	SLIST_EMPTY_((list))

#define SLIST_EMPTY_(list) \
	(SLIST_CHECK_(list), !list->head)

/**
 * Check if an item is attached to a singly-linked list.
 *
 * @param list
 *         The list to check.
 * @param item
 *         The item to check.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 * @return
 *         Whether the item is attached to the list.
 */
#define SLIST_ATTACHED(list, ...) \
	SLIST_ATTACHED_(list, __VA_ARGS__, )

#define SLIST_ATTACHED_(list, item, ...) \
	SLIST_ATTACHED__((list), (item), LIST_NEXT_(__VA_ARGS__))

#define SLIST_ATTACHED__(list, item, next) \
	(item->next || list->tail == &item->next)

/**
 * Insert an item into a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param cursor
 *         A pointer to the item to insert after, e.g. &list->head or list->tail.
 * @param item
 *         The item to insert.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define SLIST_INSERT(list, cursor, ...) \
	SLIST_INSERT_(list, cursor, __VA_ARGS__, )

#define SLIST_INSERT_(list, cursor, item, ...) \
	SLIST_INSERT__((list), (cursor), (item), LIST_NEXT_(__VA_ARGS__))

#define SLIST_INSERT__(list, cursor, item, next) LIST_VOID_( \
	bfs_assert(!SLIST_ATTACHED__(list, item, next)), \
	item->next = *cursor, \
	*cursor = item, \
	list->tail = item->next ? list->tail : &item->next)

/**
 * Add an item to the tail of a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to append.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define SLIST_APPEND(list, ...) \
	SLIST_APPEND_(list, __VA_ARGS__, )

#define SLIST_APPEND_(list, item, ...) \
	SLIST_INSERT_(list, (list)->tail, item, __VA_ARGS__)

/**
 * Add an item to the head of a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to prepend.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define SLIST_PREPEND(list, ...) \
	SLIST_PREPEND_(list, __VA_ARGS__, )

#define SLIST_PREPEND_(list, item, ...) \
	SLIST_INSERT_(list, &(list)->head, item, __VA_ARGS__)

/**
 * Add an entire singly-linked list to the tail of another.
 *
 * @param dest
 *         The destination list.
 * @param src
 *         The source list.
 */
#define SLIST_EXTEND(dest, src) \
	SLIST_EXTEND_((dest), (src))

#define SLIST_EXTEND_(dest, src) \
	(src->head ? (*dest->tail = src->head, dest->tail = src->tail, SLIST_INIT(src)) : (void)0)

/**
 * Remove an item from a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param cursor
 *         A pointer to the item to remove, either &list->head or &prev->next.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 * @return
 *         The removed item.
 */
#define SLIST_REMOVE(list, ...) \
	SLIST_REMOVE_(list, __VA_ARGS__, )

#define SLIST_REMOVE_(list, cursor, ...) \
	SLIST_REMOVE__((list), (cursor), LIST_NEXT_(__VA_ARGS__))

#define SLIST_REMOVE__(list, cursor, next) \
	(list->tail = (*cursor)->next ? list->tail : cursor, \
	 slist_remove_impl(*cursor, cursor, &(*cursor)->next, list->tail, sizeof(*cursor)))

// Helper for SLIST_REMOVE()
static inline void *slist_remove_impl(void *ret, void *cursor, void *next, void *tail, size_t size) {
	// ret = *cursor;
	// *cursor = ret->next;
	memcpy(cursor, next, size);
	// ret->next = *list->tail; (NULL)
	memcpy(next, tail, size);
	return ret;
}

/**
 * Pop the head off a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param node (optional)
 *         If specified, use head->node.next rather than head->next.
 * @return
 *         The popped item, or NULL if the list was empty.
 */
#define SLIST_POP(...) \
	SLIST_POP_(__VA_ARGS__, )

#define SLIST_POP_(list, ...) \
	SLIST_POP__((list), __VA_ARGS__)

#define SLIST_POP__(list, ...) \
	(list->head ? SLIST_REMOVE_(list, &list->head, __VA_ARGS__) : NULL)

/**
 * Loop over the items in a singly-linked list.
 *
 * @param type
 *         The list item type.
 * @param item
 *         The induction variable name.
 * @param list
 *         The list to iterate.
 * @param node (optional)
 *         If specified, use head->node.next rather than head->next.
 */
#define for_slist(type, item, ...) \
	for_slist_(type, item, __VA_ARGS__, )

#define for_slist_(type, item, list, ...) \
	for_slist__(type, item, (list), LIST_NEXT_(__VA_ARGS__))

#define for_slist__(type, item, list, next) \
	for (type *item = list->head, *_next; \
	     item && (SLIST_CHECK_(list), _next = item->next, true); \
	     item = _next)

/**
 * Initialize a doubly-linked list.
 *
 * @param list
 *         The list to initialize.
 */
#define LIST_INIT(list) \
	LIST_INIT_((list))

#define LIST_INIT_(list) \
	LIST_VOID_(list->head = list->tail = NULL)

/**
 * LIST_PREV_()       => prev
 * LIST_PREV_(node, ) => node.prev
 */
#define LIST_PREV_(...) \
	LIST_NODE_(prev, __VA_ARGS__)

/**
 * Initialize a doubly-linked list item.
 *
 * @param item
 *         The item to initialize.
 * @param node (optional)
 *         If specified, use item->node.next rather than item->next.
 */
#define LIST_ITEM_INIT(...) \
	LIST_ITEM_INIT_(__VA_ARGS__, )

#define LIST_ITEM_INIT_(item, ...) \
	LIST_ITEM_INIT__((item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_ITEM_INIT__(item, prev, next) \
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
	LIST_EMPTY_((list))

#define LIST_EMPTY_(list) \
	(LIST_CHECK_(list), !list->head)

/**
 * Add an item to the tail of a doubly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to append.
 * @param node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_APPEND(list, ...) \
	LIST_INSERT(list, (list)->tail, __VA_ARGS__)

/**
 * Add an item to the head of a doubly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to prepend.
 * @param node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_PREPEND(list, ...) \
	LIST_INSERT(list, NULL, __VA_ARGS__)

/**
 * Check if an item is attached to a doubly-linked list.
 *
 * @param list
 *         The list to check.
 * @param item
 *         The item to check.
 * @param node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 * @return
 *         Whether the item is attached to the list.
 */
#define LIST_ATTACHED(list, ...) \
	LIST_ATTACHED_(list, __VA_ARGS__, )

#define LIST_ATTACHED_(list, item, ...) \
	LIST_ATTACHED__((list), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_ATTACHED__(list, item, prev, next) \
	(item->prev || item->next || list->head == item || list->tail == item)

/**
 * Insert into a doubly-linked list after the given cursor.
 *
 * @param list
 *         The list to modify.
 * @param cursor
 *         Insert after this element.
 * @param item
 *         The item to insert.
 * @param node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_INSERT(list, cursor, ...) \
	LIST_INSERT_(list, cursor, __VA_ARGS__, )

#define LIST_INSERT_(list, cursor, item, ...) \
	LIST_INSERT__((list), (cursor), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_INSERT__(list, cursor, item, prev, next) LIST_VOID_( \
	bfs_assert(!LIST_ATTACHED__(list, item, prev, next)), \
	item->prev = cursor, \
	item->next = cursor ? cursor->next : list->head, \
	*(item->prev ? &item->prev->next : &list->head) = item, \
	*(item->next ? &item->next->prev : &list->tail) = item)

/**
 * Remove an item from a doubly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to remove.
 * @param node (optional)
 *         If specified, use item->node.{prev,next} rather than item->{prev,next}.
 */
#define LIST_REMOVE(list, ...) \
	LIST_REMOVE_(list, __VA_ARGS__, )

#define LIST_REMOVE_(list, item, ...) \
	LIST_REMOVE__((list), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_REMOVE__(list, item, prev, next) LIST_VOID_( \
	*(item->prev ? &item->prev->next : &list->head) = item->next, \
	*(item->next ? &item->next->prev : &list->tail) = item->prev, \
	item->prev = item->next = NULL)

/**
 * Loop over the items in a doubly-linked list.
 *
 * @param type
 *         The list item type.
 * @param item
 *         The induction variable name.
 * @param list
 *         The list to iterate.
 * @param node (optional)
 *         If specified, use head->node.next rather than head->next.
 */
#define for_list(type, item, ...) \
	for_list_(type, item, __VA_ARGS__, )

#define for_list_(type, item, list, ...) \
	for_list__(type, item, (list), LIST_NEXT_(__VA_ARGS__))

#define for_list__(type, item, list, next) \
	for (type *item = list->head, *_next; \
	     item && (LIST_CHECK_(list), _next = item->next, true); \
	     item = _next)

#endif // BFS_LIST_H
