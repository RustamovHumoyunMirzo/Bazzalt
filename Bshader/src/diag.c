#include "diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void sc_diag_list_init(sc_diag_list* list, sc_arena* arena) {
    list->arena = arena;
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->error_count = 0;
}

void sc_diag_add(sc_diag_list* list, sc_diag_severity severity, int line, int column, const char* fmt, ...) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        sc_diag_entry* new_entries = (sc_diag_entry*)sc_arena_alloc(list->arena, new_cap * sizeof(sc_diag_entry));
        if (list->entries) {
            memcpy(new_entries, list->entries, list->count * sizeof(sc_diag_entry));
        }
        list->entries = new_entries;
        list->capacity = new_cap;
    }

    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    sc_diag_entry* entry = &list->entries[list->count++];
    entry->severity = severity;
    entry->line = line;
    entry->column = column;
    entry->message = sc_arena_strdup(list->arena, buf);

    if (severity == SC_DIAG_ERROR) {
        list->error_count++;
    }
}

int sc_diag_has_errors(const sc_diag_list* list) {
    return list->error_count > 0;
}
