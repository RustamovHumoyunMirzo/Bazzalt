/* diag.h -- diagnostics (errors/warnings) collected during compilation.
 * Internal; arena-backed growable list used by lexer/parser/transformer. */
#ifndef BSHADER_DIAG_H
#define BSHADER_DIAG_H

#include "arena.h"

typedef enum sc_diag_severity {
    SC_DIAG_WARNING = 0,
    SC_DIAG_ERROR = 1
} sc_diag_severity;

typedef struct sc_diag_entry {
    sc_diag_severity severity;
    int line;
    int column;
    char* message; /* arena-owned */
} sc_diag_entry;

typedef struct sc_diag_list {
    sc_arena* arena;
    sc_diag_entry* entries; /* arena-allocated array, reallocated by doubling */
    size_t count;
    size_t capacity;
    int error_count;
} sc_diag_list;

void sc_diag_list_init(sc_diag_list* list, sc_arena* arena);
void sc_diag_add(sc_diag_list* list, sc_diag_severity severity, int line, int column, const char* fmt, ...);
int sc_diag_has_errors(const sc_diag_list* list);

#endif /* BSHADER_DIAG_H */
