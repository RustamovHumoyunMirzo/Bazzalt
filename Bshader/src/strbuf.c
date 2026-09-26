#include "strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

void sc_strbuf_init(sc_strbuf* sb) {
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

static void sc_strbuf_ensure(sc_strbuf* sb, size_t extra) {
    size_t needed = sb->length + extra + 1;
    if (needed <= sb->capacity) return;
    size_t new_cap = sb->capacity == 0 ? 256 : sb->capacity;
    while (new_cap < needed) new_cap *= 2;
    char* new_data = (char*)realloc(sb->data, new_cap);
    sb->data = new_data;
    sb->capacity = new_cap;
}

void sc_strbuf_append_len(sc_strbuf* sb, const char* text, size_t len) {
    sc_strbuf_ensure(sb, len);
    memcpy(sb->data + sb->length, text, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

void sc_strbuf_append(sc_strbuf* sb, const char* text) {
    sc_strbuf_append_len(sb, text, strlen(text));
}

void sc_strbuf_appendf(sc_strbuf* sb, const char* fmt, ...) {
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) { va_end(args_copy); return; }

    sc_strbuf_ensure(sb, (size_t)needed);
    vsnprintf(sb->data + sb->length, (size_t)needed + 1, fmt, args_copy);
    va_end(args_copy);
    sb->length += (size_t)needed;
}

void sc_strbuf_free(sc_strbuf* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

char* sc_strbuf_take(sc_strbuf* sb) {
    char* out = sb->data;
    if (!out) {
        out = (char*)malloc(1);
        out[0] = '\0';
    }
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    return out;
}

char* sc_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* out = (char*)malloc(len + 1);
    if (out) memcpy(out, s, len + 1);
    return out;
}
