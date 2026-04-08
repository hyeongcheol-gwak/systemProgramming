/*--------------------------------------------------------------------*/
/* heapmgr2.c                                                         */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <unistd.h>
#include "chunk.h"

#define FALSE 0
#define TRUE  1
#define NUM_BINS 32

enum { SYS_MIN_ALLOC_UNITS = 1024 };

static Chunk_T s_free_heads[NUM_BINS] = {NULL};
static void *s_heap_lo = NULL, *s_heap_hi = NULL;

/* get_bin_index:
 * Calculates the bin index for a given span.
 */
static int get_bin_index(size_t span_units) {
    if (span_units < 2) return 0;
    int idx = 0;
    size_t s = span_units;
    while (s > 1) {
        s >>= 1;
        idx++;
    }
    if (idx >= NUM_BINS) return NUM_BINS - 1;
    return idx;
}

/* check_heap_validity:
 * Provides a lightweight sanity check of the heap state from the  
 * caller's perspective. It evaluates if the current configured heap 
 * mapping is correct.
 */
#ifndef NDEBUG
static int check_heap_validity(void) {
    Chunk_T w;

    if (s_heap_lo == NULL || s_heap_hi == NULL) return (s_heap_lo == s_heap_hi);

    if (s_heap_lo == s_heap_hi) {
        for (int i = 0; i < NUM_BINS; i++) {
            if (s_free_heads[i] != NULL) return FALSE;
        }
        return TRUE;
    }

    for (w = (Chunk_T)s_heap_lo;
         w && (void*)w < s_heap_hi;
         w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
    }

    for (int i = 0; i < NUM_BINS; i++) {
        for (w = s_free_heads[i]; w; w = chunk_get_next_free(w)) {
            if (chunk_get_status(w) != CHUNK_FREE) return FALSE;
            if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
        }
    }
    return TRUE;
}
#endif

/* bytes_to_span_units:
 * Converts a byte request from the caller to the minimum required 
 * chunk span layout (padding inclusively).
 */
static size_t bytes_to_span_units(size_t bytes) {
    size_t total_bytes = bytes + 2 * sizeof(size_t);
    size_t units = (total_bytes + (CHUNK_UNIT - 1)) / CHUNK_UNIT;
    return units < CHUNK_MIN_UNITS ? CHUNK_MIN_UNITS : units;
}

/* header_from_payload:
 * Converts a valid caller's block data pointer back into the 
 * internal chunk header format.
 */
static Chunk_T header_from_payload(void *p) {
    return (Chunk_T)((char *)p - sizeof(size_t));
}

/* heap_bootstrap:
 * Bootstraps the local heap via sbrk requests to the system OS 
 * upon first initialization from the caller.
 */
static void heap_bootstrap(void) {
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) exit(-1);
    for (int i = 0; i < NUM_BINS; i++) {
        s_free_heads[i] = NULL;
    }
}

/* freelist_insert:
 * Puts a newly available block c at the front of the free list. 
 * Expected caller behavior mapping internal updates reliably.
 */
static void freelist_insert(Chunk_T c) {
    int idx = get_bin_index(chunk_get_span_units(c));
    chunk_set_next_free(c, s_free_heads[idx]);
    chunk_set_prev_free(c, NULL);
    if (s_free_heads[idx] != NULL) {
        chunk_set_prev_free(s_free_heads[idx], c);
    }
    s_free_heads[idx] = c;
}

/* freelist_remove:
 * Transparently isolates block c from the free list dependencies 
 * so the caller can directly assume chunk allocations safely.
 */
static void freelist_remove(Chunk_T c) {
    int idx = get_bin_index(chunk_get_span_units(c));
    Chunk_T p = chunk_get_prev_free(c);
    Chunk_T n = chunk_get_next_free(c);
    if (p != NULL) chunk_set_next_free(p, n);
    else s_free_heads[idx] = n;
    if (n != NULL) chunk_set_prev_free(n, p);
}

/* split_for_alloc:
 * Returns to the caller a specifically sliced allocated block subset 
 * matching perfectly their exact payload requirements.
 */
static Chunk_T split_for_alloc(Chunk_T c, size_t need_units) {
    int old_span = chunk_get_span_units(c);
    int remain_span = old_span - (int)need_units;

    if (remain_span < CHUNK_MIN_UNITS) {
        freelist_remove(c);
        chunk_set_status(c, CHUNK_USED);
        return c;
    }

    freelist_remove(c);

    chunk_set_span_units(c, remain_span);
    chunk_set_status(c, CHUNK_FREE);
    freelist_insert(c);

    Chunk_T alloc = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
    chunk_set_span_units(alloc, (int)need_units);
    chunk_set_status(alloc, CHUNK_USED);
    return alloc;
}

/* sys_grow_and_coalesce:
 * Returns newly expanded heap memory dynamically generated from sbrk
 * so the caller receives adequately sized initial bulk space.
 */
static Chunk_T sys_grow_and_coalesce(size_t need_units) {
    Chunk_T c;
    size_t grow_span = need_units < SYS_MIN_ALLOC_UNITS ?
                       SYS_MIN_ALLOC_UNITS : need_units;

    c = (Chunk_T)sbrk(grow_span * CHUNK_UNIT);
    if (c == (Chunk_T)-1) return NULL;
    s_heap_hi = sbrk(0);

    chunk_set_span_units(c, (int)grow_span);
    chunk_set_status(c, CHUNK_USED);
    return c;
}

/* heapmgr_malloc:
 * Request dynamic allocation sizing. Returns to the caller a point
 * of access to highly optimized and completely free payload memory.
 */
void *heapmgr_malloc(size_t size) {
    static int booted = FALSE;
    Chunk_T cur = NULL;
    size_t need_units;

    if (size == 0) return NULL;
    if (!booted) { heap_bootstrap(); booted = TRUE; }
    assert(check_heap_validity());

    need_units = bytes_to_span_units(size);
    int start_idx = get_bin_index(need_units);

    /* Phase 1: Search the exact bin (First-Fit) */
    for (cur = s_free_heads[start_idx]; cur; cur = chunk_get_next_free(cur)) {
        if ((size_t)chunk_get_span_units(cur) >= need_units) {
            break;
        }
    }

    /* Phase 2: If not found, find the first available completely larger bin */
    if (cur == NULL) {
        for (int i = start_idx + 1; i < NUM_BINS; i++) {
            if (s_free_heads[i] != NULL) {
                cur = s_free_heads[i];
                break;
            }
        }
    }

    if (cur != NULL) {
        cur = split_for_alloc(cur, need_units);
        assert(check_heap_validity());
        return (void *)((char *)cur + sizeof(size_t));
    }

    /* Phase 3: Grow Heap */
    cur = sys_grow_and_coalesce(need_units);
    if (cur == NULL) return NULL;

    Chunk_T prev;
    prev = chunk_get_prev_adjacent(cur, s_heap_lo, s_heap_hi);
    if (prev != NULL && chunk_get_status(prev) == CHUNK_FREE) {
        freelist_remove(prev);
        int p_sz = chunk_get_span_units(prev);
        int c_sz = chunk_get_span_units(cur);
        chunk_set_span_units(prev, p_sz + c_sz);
        cur = prev;
    }
    
    chunk_set_status(cur, CHUNK_FREE);
    freelist_insert(cur);
    cur = split_for_alloc(cur, need_units);

    assert(check_heap_validity());
    return (void *)((char *)cur + sizeof(size_t));
}

/* heapmgr_free:
 * Accepts a previously allocated pointer from the caller mapping its
 * payload backwards into purely optimized reusable free storage blocks.
 */
void heapmgr_free(void *p) {
    Chunk_T c, prev, next;

    if (p == NULL) return;
    assert(check_heap_validity());

    c = header_from_payload(p);
    assert(chunk_get_status(c) == CHUNK_USED);

    prev = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi);
    next = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);

    int p_f = (prev != NULL && chunk_get_status(prev) == CHUNK_FREE);
    int n_f = (next != NULL && chunk_get_status(next) == CHUNK_FREE);

    if (!p_f && !n_f) {
        chunk_set_status(c, CHUNK_FREE);
        freelist_insert(c);
    } else if (p_f && !n_f) {
        freelist_remove(prev);
        int p_sz = chunk_get_span_units(prev);
        int c_sz = chunk_get_span_units(c);
        chunk_set_span_units(prev, p_sz + c_sz);
        chunk_set_status(prev, CHUNK_FREE);
        freelist_insert(prev);
    } else if (!p_f && n_f) {
        freelist_remove(next);
        int c_sz = chunk_get_span_units(c);
        int n_sz = chunk_get_span_units(next);
        chunk_set_span_units(c, c_sz + n_sz);
        chunk_set_status(c, CHUNK_FREE);
        freelist_insert(c);
    } else {
        freelist_remove(prev);
        freelist_remove(next);
        int p_sz = chunk_get_span_units(prev);
        int c_sz = chunk_get_span_units(c);
        int n_sz = chunk_get_span_units(next);
        chunk_set_span_units(prev, p_sz + c_sz + n_sz);
        chunk_set_status(prev, CHUNK_FREE);
        freelist_insert(prev);
    }

    assert(check_heap_validity());
}
