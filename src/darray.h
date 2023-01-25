// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A dynamic array library.
 *
 * darrays are represented by a simple pointer to the array element type, like
 * any other array.  Behind the scenes, the capacity and current length of the
 * array are stored along with it.  NULL is a valid way to initialize an empty
 * darray:
 *
 *     int *darray = NULL;
 *
 * To append an element to a darray, use the DARRAY_PUSH macro:
 *
 *     int e = 42;
 *     if (DARRAY_PUSH(&darray, &e) != 0) {
 *             // Report the error...
 *     }
 *
 * The length can be retrieved by darray_length().  Iterating over the array
 * works like normal arrays:
 *
 *     for (size_t i = 0; i < darray_length(darray); ++i) {
 *             printf("%d\n", darray[i]);
 *     }
 *
 * To free a darray, use darray_free():
 *
 *     darray_free(darray);
 */

#ifndef BFS_DARRAY_H
#define BFS_DARRAY_H

#include <stddef.h>

/**
 * Get the length of a darray.
 *
 * @param da
 *         The array in question.
 * @return
 *         The length of the array.
 */
size_t darray_length(const void *da);

/**
 * @internal Use DARRAY_PUSH().
 *
 * Push an element into a darray.
 *
 * @param da
 *         The array to append to.
 * @param item
 *         The item to append.
 * @param size
 *         The size of the item.
 * @return
 *         The (new) location of the array.
 */
void *darray_push(void *da, const void *item, size_t size);

/**
 * @internal Use DARRAY_PUSH().
 *
 * Check if the last darray_push() call failed.
 *
 * @param da
 *         The darray to check.
 * @return
 *         0 on success, -1 on failure.
 */
int darray_check(void *da);

/**
 * Free a darray.
 *
 * @param da
 *         The darray to free.
 */
void darray_free(void *da);

/**
 * Push an item into a darray.
 *
 * @param da
 *         The array to append to.
 * @param item
 *         A pointer to the item to append.
 * @return
 *         0 on success, -1 on failure.
 */
#define DARRAY_PUSH(da, item) \
	(darray_check(*(da) = darray_push(*(da), (item), sizeof(**(da) = *(item)))))

#endif // BFS_DARRAY_H
