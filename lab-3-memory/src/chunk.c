/*--------------------------------------------------------------------*/
/* chunk.c                                                            */
/*--------------------------------------------------------------------*/

#include "chunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/*
 * Internal structure. We only define the Free variant.
 * Allocated chunk payload overlays 'next' and 'prev'.
 */
struct Chunk {
    size_t  header; /* lowest bit: status. upper: span_units */
    Chunk_T next;
    Chunk_T prev;
};

/* get_footer:
 * Returns a pointer to the footer of a chunk.
 */
static size_t* get_footer(Chunk_T c) {
    size_t span = c->header >> 1;
    char* base = (char*)c;
    return (size_t*)(base + (span * CHUNK_UNIT) - sizeof(size_t));
}

/* chunk_get_status:
 * Returns CHUNK_FREE or CHUNK_USED. 
 */
int chunk_get_status(Chunk_T c) {
    return (int)(c->header & 1);
}

/* chunk_set_status:
 * Sets the status in the header and footer.
 */
void chunk_set_status(Chunk_T c, int status) {
    size_t span = c->header >> 1;
    size_t val = (span << 1) | (size_t)(status & 1);
    c->header = val;
    *get_footer(c) = val;
}

/* chunk_get_span_units:
 * Returns the size of the chunk in units.
 */
int chunk_get_span_units(Chunk_T c) {
    return (int)(c->header >> 1);
}

/* chunk_set_span_units:
 * Sets the size of the chunk in header and footer.
 */
void chunk_set_span_units(Chunk_T c, int span_units) {
    int status = (int)(c->header & 1);
    size_t val = (((size_t)span_units) << 1) | (size_t)(status & 1);
    c->header = val;
    *get_footer(c) = val;
}

/* chunk_get_next_free:
 * Returns the next free chunk pointer.
 */
Chunk_T chunk_get_next_free(Chunk_T c) {
    return c->next;
}

/* chunk_set_next_free:
 * Sets the next free chunk pointer.
 */
void chunk_set_next_free(Chunk_T c, Chunk_T next) {
    c->next = next;
}

/* chunk_get_prev_free:
 * Returns the previous free chunk pointer.
 */
Chunk_T chunk_get_prev_free(Chunk_T c) {
    return c->prev;
}

/* chunk_set_prev_free:
 * Sets the previous free chunk pointer.
 */
void chunk_set_prev_free(Chunk_T c, Chunk_T prev) {
    c->prev = prev;
}

/* chunk_get_adjacent:
 * Returns the next adjacent chunk in memory.
 */
Chunk_T chunk_get_adjacent(Chunk_T c, void *start, void *end) {
    size_t span = c->header >> 1;
    char* base = (char*)c;
    Chunk_T n = (Chunk_T)(base + span * CHUNK_UNIT);
    if ((void*)n >= end) return NULL;
    return n;
}

/* chunk_get_prev_adjacent:
 * Returns the previous adjacent chunk in memory using the footer.
 */
Chunk_T chunk_get_prev_adjacent(Chunk_T c, void *start, void *end) {
    char* base = (char*)c;
    if ((void*)base <= start) return NULL;
    size_t *prev_footer = (size_t*)(base - sizeof(size_t));
    size_t prev_span = (*prev_footer) >> 1;
    Chunk_T p = (Chunk_T)(base - prev_span * CHUNK_UNIT);
    return p;
}

#ifndef NDEBUG
/* chunk_is_valid:
 * Validates chunk bounds, span, and header-footer matching.
 */
int chunk_is_valid(Chunk_T c, void *start, void *end) {
    if (c == NULL || start == NULL || end == NULL) return 0;
    if ((void*)c < start || (void*)c >= end) {
        fprintf(stderr, "Chunk out of bounds\n");
        return 0;
    }
    if (chunk_get_span_units(c) < CHUNK_MIN_UNITS) {
        fprintf(stderr, "Chunk span too small\n");
        return 0;
    }
    if (c->header != *get_footer(c)) {
        fprintf(stderr, "Header/footer mismatch\n");
        return 0;
    }
    return 1;
}
#endif