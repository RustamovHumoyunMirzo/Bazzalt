/* strset.h -- tiny malloc-backed string set (linear scan, fine at the
 * scale of a single shader's property/local names). Internal; shared by
 * generator_filament.c and transform.c (via transform_internal.h) so
 * transforms can check whether an identifier refers to a property. */
#ifndef BSHADER_STRSET_H
#define BSHADER_STRSET_H

#include <stddef.h>

typedef struct sc_strset {
    char** items;
    size_t count;
    size_t cap;
} sc_strset;

void sc_strset_init(sc_strset* s);
int sc_strset_contains(const sc_strset* s, const char* name);
/* Adds by reference (does not copy); caller guarantees the pointer
 * outlives the set. */
void sc_strset_add(sc_strset* s, const char* name);
void sc_strset_push(sc_strset* s, const char* name);
void sc_strset_free(sc_strset* s);

#endif /* BSHADER_STRSET_H */
