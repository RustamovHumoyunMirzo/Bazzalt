#include "strset.h"
#include <stdlib.h>
#include <string.h>

void sc_strset_init(sc_strset* s) {
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
}

int sc_strset_contains(const sc_strset* s, const char* name) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], name) == 0) return 1;
    }
    return 0;
}

void sc_strset_add(sc_strset* s, const char* name) {
    if (sc_strset_contains(s, name)) return;
    sc_strset_push(s,name);
}

void sc_strset_push(sc_strset* s,const char* name){
    if (s->count == s->cap) {
        size_t new_cap = s->cap == 0 ? 8 : s->cap * 2;
        s->items = (char**)realloc(s->items, new_cap * sizeof(char*));
        s->cap = new_cap;
    }
    s->items[s->count++] = (char*)name;
}

void sc_strset_free(sc_strset* s) {
    free(s->items);
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
}
