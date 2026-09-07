/*
 * Freestanding WebAssembly wrapper for dshuf
 * Compile with:
 *   clang --target=wasm32 -O3 -nostdlib -Wl,--no-entry -Wl,--export-all dshuf_wasm.c -o dshuf.wasm
 */

#include <stddef.h>
#include <stdint.h>

/* Bump allocator with a first-fit free list so stream_free can recycle. */
extern unsigned char __heap_base;
static unsigned char *bump_ptr = &__heap_base;

typedef struct dshuf_free_block {
    size_t size;
    struct dshuf_free_block *next;
} dshuf_free_block;

static dshuf_free_block *dshuf_free_list = 0;

void *malloc(size_t size);
void free(void *ptr);

void *malloc(size_t size) {
    if (size < sizeof(void *)) {
        size = sizeof(void *);
    }
    size = (size + 7) & ~7;

    dshuf_free_block **cur = &dshuf_free_list;
    while (*cur) {
        if ((*cur)->size >= size) {
            dshuf_free_block *blk = *cur;
            size_t blk_size = blk->size;
            *cur = blk->next;
            size_t *header = (size_t *)blk;
            *header = blk_size;
            return (void *)(header + 1);
        }
        cur = &(*cur)->next;
    }

    size_t *header = (size_t *)bump_ptr;
    *header = size;
    bump_ptr += size + sizeof(size_t);
    return (void *)(header + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    size_t *header = (size_t *)ptr - 1;
    dshuf_free_block *blk = (dshuf_free_block *)header;
    blk->size = *header;
    blk->next = dshuf_free_list;
    dshuf_free_list = blk;
}

void *realloc(void *ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    size_t old_size = *((size_t *)ptr - 1);
    if (old_size >= new_size) return ptr;
    void *new_p = malloc(new_size);
    size_t copy_size = old_size;
    for (size_t i = 0; i < copy_size; i++) {
        ((char *)new_p)[i] = ((const char *)ptr)[i];
    }
    free(ptr);
    return new_p;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = (char *)s;
    for (size_t i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

#define DSHUF_NO_STDLIB
#define DSHUF_IMPLEMENTATION
#include "../../dshuf.h"