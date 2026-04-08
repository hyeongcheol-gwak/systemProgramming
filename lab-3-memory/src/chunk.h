/*--------------------------------------------------------------------*/
/* chunk.h                                                            */
/*--------------------------------------------------------------------*/

#ifndef _CHUNK_H_
#define _CHUNK_H_

#include <stddef.h>

/*
 * A standard explicit doubly-linked list chunk layout:
 * - Header (sizeof(size_t) bytes): [ span (size in units) | status bit ]
 * - Next Free Ptr: used only when free
 * - Prev Free Ptr: used only when free
 * - Footer (sizeof(size_t) bytes): [ span | status ]
 */

typedef struct Chunk *Chunk_T;

/* Status flags */
enum {
    CHUNK_FREE = 0,
    CHUNK_USED = 1
};

/* Chunk properties */
enum {
    CHUNK_UNIT = 16,
    CHUNK_MIN_UNITS = 2 /* Minimum 32 bytes */
};

/* chunk_get_status:
 * Returns the status (CHUNK_FREE or CHUNK_USED) of the given chunk. 
 */
int chunk_get_status(Chunk_T c);

/* chunk_set_status:
 * Sets the status of the chunk's header and footer. 
 */
void chunk_set_status(Chunk_T c, int status);

/* chunk_get_span_units:
 * Returns the chunk's size in CHUNK_UNITs, including header & footer. 
 */
int chunk_get_span_units(Chunk_T c);

/* chunk_set_span_units:
 * Sets the chunk's size (in CHUNK_UNITs) in header and footer. 
 */
void chunk_set_span_units(Chunk_T c, int span_units);

/* chunk_get_next_free:
 * Returns the pointer to the next free chunk in the doubly-linked list.
 */
Chunk_T chunk_get_next_free(Chunk_T c);

/* chunk_set_next_free:
 * Sets the next free chunk pointer in the doubly-linked list.
 */
void chunk_set_next_free(Chunk_T c, Chunk_T next);

/* chunk_get_prev_free:
 * Returns the pointer to the previous free chunk.
 */
Chunk_T chunk_get_prev_free(Chunk_T c);

/* chunk_set_prev_free:
 * Sets the previous free chunk pointer.
 */
void chunk_set_prev_free(Chunk_T c, Chunk_T prev);

/* chunk_get_adjacent:
 * Returns the physically adjacent (higher addresses) chunk, or NULL 
 * if it goes out of bounds.
 */
Chunk_T chunk_get_adjacent(Chunk_T c, void *start, void *end);

/* chunk_get_prev_adjacent:
 * Returns the physically adjacent (lower addresses) chunk, or NULL 
 * if it goes out of bounds. Uses the preceding footer.
 */
Chunk_T chunk_get_prev_adjacent(Chunk_T c, void *start, void *end);

#ifndef NDEBUG
/* chunk_is_valid:
 * Checks if a chunk is structurally valid.
 */
int chunk_is_valid(Chunk_T c, void *start, void *end);
#endif

#endif /* _CHUNK_H_ */