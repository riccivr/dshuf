/*
 * Freestanding WebAssembly wrapper for dshuf
 * Compile with:
 *   clang --target=wasm32 -O3 -nostdlib -Wl,--no-entry -Wl,--export-all dshuf_wasm.c -o dshuf.wasm
 */

#include <stddef.h>
#include <stdint.h>

/* Minimal bump allocator */
extern unsigned char __heap_base;
static unsigned char *bump_ptr = &__heap_base;

void *malloc(size_t size) {
    size = (size + 7) & ~7;
    void *p = bump_ptr;
    bump_ptr += size;
    return p;
}

void *realloc(void *ptr, size_t size) {
    void *new_p = malloc(size);
    if (ptr) {
        char *d = (char *)new_p;
        const char *s = (const char *)ptr;
        for (size_t i = 0; i < size; i++) d[i] = s[i];
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