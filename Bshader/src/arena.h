/* arena.h -- internal bump allocator used for all AST/parse-time memory.
 * Internal helper: snake_case naming, not part of the public API. */
#ifndef BSHADER_ARENA_H
#define BSHADER_ARENA_H

#include <stddef.h>

typedef struct sc_arena_block {
    struct sc_arena_block* next;
    size_t size;
    size_t used;
    char data[];
} sc_arena_block;

typedef struct sc_arena {
    sc_arena_block* head;
    size_t default_block_size;
} sc_arena;

void sc_arena_init(sc_arena* arena, size_t default_block_size);
void* sc_arena_alloc(sc_arena* arena, size_t size);
void* sc_arena_alloc_zeroed(sc_arena* arena, size_t size);
char* sc_arena_strdup(sc_arena* arena, const char* str);
char* sc_arena_strndup(sc_arena* arena, const char* str, size_t len);
void sc_arena_reset(sc_arena* arena);
void sc_arena_free(sc_arena* arena);

#endif /* BSHADER_ARENA_H */
