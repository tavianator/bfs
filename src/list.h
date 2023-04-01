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
 *     SLIST_APPEND(&items.queue, &item, chain);
 *     LIST_APPEND(&items.cache, &item, lru);
 */

#ifndef BFS_LIST_H
#define BFS_LIST_H

#include <stddef.h>

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
	LIST_BLOCK_(SLIST_INIT_((list)))

#define SLIST_INIT_(list) \
	list->head = NULL; \
	list->tail = &list->head;

/**
 * Wraps a group of statements in a block.
 */
#define LIST_BLOCK_(block) do { block } while (0)

/**
 * Add an item to the tail of a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to append.
 * @param link (optional)
 *         If specified, use item->link.next rather than item->next.
 *
 * ---
 *
 * We play some tricks with variadic macros to handle the optional parameter:
 *
 *     SLIST_APPEND(list, item) => {
 *             *list->tail = item;
 *             list->tail = &item->next;
 *     }
 *
 *     SLIST_APPEND(list, item, link) => {
 *             *list->tail = item;
 *             list->tail = &item->link.next;
 *     }
 *
 * The first trick is that
 *
 *     #define SLIST_APPEND(list, item, ...)
 *
 * won't work because both commas are required (until C23; see N3033). As a
 * workaround, we dispatch to another macro and add a trailing comma.
 *
 *     SLIST_APPEND(list, item)       => SLIST_APPEND_(list, item, )
 *     SLIST_APPEND(list, item, link) => SLIST_APPEND_(list, item, link, )
 */
#define SLIST_APPEND(list, ...) SLIST_APPEND_(list, __VA_ARGS__, )

/**
 * Now we need a way to generate either ->next or ->link.next depending on
 * whether the link parameter was passed.  The approach is based on
 *
 *     #define FOO(...) BAR(__VA_ARGS__, 1, 2, )
 *     #define BAR(x, y, z, ...) z
 *
 *     FOO(a)    => 2
 *     FOO(a, b) => 1
 *
 * The LIST_NEXT_() macro uses this technique:
 *
 *     LIST_NEXT_()       => LIST_LINK_(next, )
 *     LIST_NEXT_(link, ) => LIST_LINK_(next, link, )
 */
#define LIST_NEXT_(...) LIST_LINK_(next, __VA_ARGS__)

/**
 * LIST_LINK_() dispatches to yet another macro:
 *
 *     LIST_LINK_(next, )       => LIST_LINK__(next,     , . ,   , )
 *     LIST_LINK_(next, link, ) => LIST_LINK__(next, link,   , . , , )
 */
#define LIST_LINK_(dir, ...) LIST_LINK__(dir, __VA_ARGS__, . , , )

/**
 * And finally, LIST_LINK__() adds the link and the dot if necessary.
 *
 *                 dir   link ignored dot
 *                  v     v      v     v
 *     LIST_LINK__(next,     ,   .   ,   , )   => next
 *     LIST_LINK__(next, link,       , . , , ) => link . next
 *                  ^     ^      ^     ^
 *                 dir   link ignored dot
 */
#define LIST_LINK__(dir, link, ignored, dot, ...) link dot dir

/**
 * SLIST_APPEND_() uses LIST_NEXT_() to generate the right name for the list
 * link, and finally delegates to the actual implementation.
 *
 *     SLIST_APPEND_(list, item, )       => SLIST_APPEND__((list), (item), next)
 *     SLIST_APPEND_(list, item, link, ) => SLIST_APPEND__((list), (item), link.next)
 */
#define SLIST_APPEND_(list, item, ...) \
	LIST_BLOCK_(SLIST_APPEND__((list), (item), LIST_NEXT_(__VA_ARGS__)))

#define SLIST_APPEND__(list, item, next) \
	*list->tail = item; \
	list->tail = &item->next;

/**
 * Add an item to the head of a singly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to prepend.
 * @param link (optional)
 *         If specified, use item->link.next rather than item->next.
 */
#define SLIST_PREPEND(list, ...) SLIST_PREPEND_(list, __VA_ARGS__, )

#define SLIST_PREPEND_(list, item, ...) \
	LIST_BLOCK_(SLIST_PREPEND__((list), (item), LIST_NEXT_(__VA_ARGS__)))

#define SLIST_PREPEND__(list, item, next) \
	list->tail = list->head ? list->tail : &item->next; \
	item->next = list->head; \
	list->head = item;

/**
 * Add an entire singly-linked list to the tail of another.
 *
 * @param dest
 *         The destination list.
 * @param src
 *         The source list.
 */
#define SLIST_EXTEND(dest, src) \
	LIST_BLOCK_(SLIST_EXTEND_((dest), (src)))

#define SLIST_EXTEND_(dest, src) \
	if (src->head) { \
		*dest->tail = src->head; \
		dest->tail = src->tail; \
		SLIST_INIT(src); \
	}

/**
 * Pop the head off a singly-linked list.
 *
 * @param list
 *         The list to pop from.
 * @param link (optional)
 *         If specified, use head->link.next rather than head->next.
 */
#define SLIST_POP(...) SLIST_POP_(__VA_ARGS__, )

#define SLIST_POP_(list, ...) \
	LIST_BLOCK_(SLIST_POP__((list), LIST_NEXT_(__VA_ARGS__)))

#define SLIST_POP__(list, next) \
	void *_next = (void *)list->head->next; \
	list->head->next = NULL; \
	list->head = _next; \
	list->tail = list->head ? list->tail : &list->head;

/**
 * Initialize a doubly-linked list.
 *
 * @param list
 *         The list to initialize.
 */
#define LIST_INIT(list) \
	LIST_BLOCK_(LIST_INIT_((list)))

#define LIST_INIT_(list) \
	list->head = list->tail = NULL;

/**
 * LIST_PREV_()       => prev
 * LIST_PREV_(link, ) => link.prev
 */
#define LIST_PREV_(...) LIST_LINK_(prev, __VA_ARGS__)

/**
 * Add an item to the tail of a doubly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to append.
 * @param link (optional)
 *         If specified, use item->link.{prev,next} rather than item->{prev,next}.
 */
#define LIST_APPEND(list, ...) LIST_INSERT(list, (list)->tail, __VA_ARGS__)

/**
 * Add an item to the head of a doubly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to prepend.
 * @param link (optional)
 *         If specified, use item->link.{prev,next} rather than item->{prev,next}.
 */
#define LIST_PREPEND(list, ...) LIST_INSERT(list, NULL, __VA_ARGS__)

/**
 * Insert into a doubly-linked list after the given cursor.
 *
 * @param list
 *         The list to initialize.
 * @param cursor
 *         Insert after this element.
 * @param item
 *         The item to insert.
 * @param link (optional)
 *         If specified, use item->link.{prev,next} rather than item->{prev,next}.
 */
#define LIST_INSERT(list, cursor, ...) LIST_INSERT_(list, cursor, __VA_ARGS__, )

#define LIST_INSERT_(list, cursor, item, ...) \
	LIST_BLOCK_(LIST_INSERT__((list), (cursor), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__)))

#define LIST_INSERT__(list, cursor, item, prev, next) \
	item->prev = cursor; \
	item->next = cursor ? cursor->next : list->head; \
	*(item->prev ? &item->prev->next : &list->head) = item; \
	*(item->next ? &item->next->prev : &list->tail) = item;

/**
 * Remove an item from a doubly-linked list.
 *
 * @param list
 *         The list to modify.
 * @param item
 *         The item to remove.
 * @param link (optional)
 *         If specified, use item->link.{prev,next} rather than item->{prev,next}.
 */
#define LIST_REMOVE(list, ...) LIST_REMOVE_(list, __VA_ARGS__, )

#define LIST_REMOVE_(list, item, ...) \
	LIST_BLOCK_(LIST_REMOVE__((list), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__)))

#define LIST_REMOVE__(list, item, prev, next) \
	*(item->prev ? &item->prev->next : &list->head) = item->next; \
	*(item->next ? &item->next->prev : &list->tail) = item->prev; \
	item->prev = item->next = NULL;

/**
 * Check if an item is attached to a doubly-linked list.
 *
 * @param list
 *         The list to check.
 * @param item
 *         The item to check.
 * @param link (optional)
 *         If specified, use item->link.{prev,next} rather than item->{prev,next}.
 * @return
 *         Whether the item is attached to the list.
 */
#define LIST_ATTACHED(list, ...) LIST_ATTACHED_(list, __VA_ARGS__, )

#define LIST_ATTACHED_(list, item, ...) \
	LIST_ATTACHED__((list), (item), LIST_PREV_(__VA_ARGS__), LIST_NEXT_(__VA_ARGS__))

#define LIST_ATTACHED__(list, item, prev, next) \
	(item->prev || item->next || list->head == item || list->tail == item)

#endif // BFS_LIST_H
