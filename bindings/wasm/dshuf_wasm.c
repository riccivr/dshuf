/*
 * Freestanding WebAssembly wrapper for dshuf
 * Compile with:
 *   clang --target=wasm32 -O3 -nostdlib -Wl,--no-entry -Wl,--export-all dshuf_wasm.c -o dshuf.wasm
 */

#include <stddef.h>
#include <stdint.h>

/* Minimal bump allocator with tracked allocation size */
extern unsigned char __heap_base;
static unsigned char *bump_ptr = &__heap_base;

void *malloc(size_t size) {
    size = (size + 7) & ~7;
    size_t *header = (size_t *)bump_ptr;
    *header = size;
    bump_ptr += size + sizeof(size_t);
    return (void *)(header + 1);
}

void *realloc(void *ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    size_t old_size = *((size_t *)ptr - 1);
    void *new_p = malloc(new_size);
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    for (size_t i = 0; i < copy_size; i++) {
        ((char *)new_p)[i] = ((const char *)ptr)[i];
    }
    return new_p;
}

void free(void *ptr) {
    (void)ptr;
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