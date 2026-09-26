#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define SC_ARENA_ALIGN 16u
#define SC_ARENA_DEFAULT_BLOCK (64u * 1024u)

static size_t sc_align_up(size_t n, size_t align) {
    return (n + (align - 1)) & ~(align - 1);
}

void sc_arena_init(sc_arena* arena, size_t default_block_size) {
    arena->head = NULL;
    arena->default_block_size = default_block_size ? default_block_size : SC_ARENA_DEFAULT_BLOCK;
}

static sc_arena_block* sc_arena_new_block(size_t min_size) {
    size_t size = min_size;
    sc_arena_block* block = (sc_arena_block*)malloc(sizeof(sc_arena_block) + size);
    if (!block) return NULL;
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

void* sc_arena_alloc(sc_arena* arena, size_t size) {
    size_t aligned = sc_align_up(size, SC_ARENA_ALIGN);

    if (!arena->head || arena->head->used + aligned > arena->head->size) {
        size_t block_size = arena->default_block_size;
        if (aligned + 64 > block_size) {
            block_size = aligned + 64;
        }
        sc_arena_block* block = sc_arena_new_block(block_size);
        if (!block) return NULL;
        block->next = arena->head;
        arena->head = block;
    }

    void* ptr = arena->head->data + arena->head->used;
    arena->head->used += aligned;
    return ptr;
}

void* sc_arena_alloc_zeroed(sc_arena* arena, size_t size) {
    void* ptr = sc_arena_alloc(arena, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

char* sc_arena_strdup(sc_arena* arena, const char* str) {
    size_t len = strlen(str);
    return sc_arena_strndup(arena, str, len);
}

char* sc_arena_strndup(sc_arena* arena, const char* str, size_t len) {
    char* out = (char*)sc_arena_alloc(arena, len + 1);
    if (!out) return NULL;
    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}

void sc_arena_reset(sc_arena* arena) {
    /* Free all but the first (largest, most recently allocated) block,
     * then mark it empty. This keeps at least one warm block around for
     * the next compile when the context is reused. */
    if (!arena->head) return;

    sc_arena_block* keep = arena->head;
    sc_arena_block* cur = keep->next;
    while (cur) {
        sc_arena_block* next = cur->next;
        free(cur);
        cur = next;
    }
    keep->next = NULL;
    keep->used = 0;
}

void sc_arena_free(sc_arena* arena) {
    sc_arena_block* cur = arena->head;
    while (cur) {
        sc_arena_block* next = cur->next;
        free(cur);
        cur = next;
    }
    arena->head = NULL;
}
