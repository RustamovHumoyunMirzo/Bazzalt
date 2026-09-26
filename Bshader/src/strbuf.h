/* strbuf.h -- tiny malloc-backed growable string buffer. Internal.
 * Used by the generator to assemble output text; unlike AST memory this
 * is NOT arena-backed because compiled output must outlive the context
 * (and its arena) that produced it -- see shader_translator.h lifecycle
 * notes on ShaderCompilationResult. */
#ifndef BSHADER_STRBUF_H
#define BSHADER_STRBUF_H

#include <stddef.h>

typedef struct sc_strbuf {
    char* data;
    size_t length;
    size_t capacity;
} sc_strbuf;

void sc_strbuf_init(sc_strbuf* sb);
void sc_strbuf_append(sc_strbuf* sb, const char* text);
void sc_strbuf_append_len(sc_strbuf* sb, const char* text, size_t len);
void sc_strbuf_appendf(sc_strbuf* sb, const char* fmt, ...);
void sc_strbuf_free(sc_strbuf* sb);
/* Detach the internal buffer (caller now owns it, must free() it) and
 * reset sb to an empty, still-usable state. */
char* sc_strbuf_take(sc_strbuf* sb);

/* malloc-backed strdup: written locally because plain C11 (no GNU/POSIX
 * extensions) does not guarantee strdup() is declared. */
char* sc_strdup(const char* s);

#endif /* BSHADER_STRBUF_H */
