/* lexer.h -- hand-written lexer for the Bshader language. Internal. */
#ifndef BSHADER_LEXER_H
#define BSHADER_LEXER_H

#include <stddef.h>
#include "arena.h"

typedef enum sc_token_kind {
    SC_TOK_EOF = 0,

    SC_TOK_IDENTIFIER,
    SC_TOK_NUMBER,
    SC_TOK_STRING,

    /* keywords */
    SC_TOK_KW_SHADER,
    SC_TOK_KW_PROPERTIES,
    SC_TOK_KW_MATERIAL,
    SC_TOK_KW_IF,
    SC_TOK_KW_ELSE,
    SC_TOK_KW_TRUE,
    SC_TOK_KW_FALSE,

    /* type keywords (also valid as ordinary type-name identifiers) */
    SC_TOK_KW_FLOAT,
    SC_TOK_KW_INT,
    SC_TOK_KW_BOOL,
    SC_TOK_KW_VEC2,
    SC_TOK_KW_VEC3,
    SC_TOK_KW_VEC4,
    SC_TOK_KW_TEXTURE2D,
    SC_TOK_KW_MAT3,
    SC_TOK_KW_MAT4,
    SC_TOK_KW_VERTEX,
    SC_TOK_KW_OPTIONS,
    SC_TOK_KW_FOR,
    SC_TOK_KW_WHILE,
    SC_TOK_KW_BREAK,
    SC_TOK_KW_CONTINUE,
    SC_TOK_LBRACKET,
    SC_TOK_RBRACKET,
    SC_TOK_PLUS_PLUS,
    SC_TOK_MINUS_MINUS,

    /* punctuation */
    SC_TOK_LBRACE,    /* { */
    SC_TOK_RBRACE,    /* } */
    SC_TOK_LPAREN,    /* ( */
    SC_TOK_RPAREN,    /* ) */
    SC_TOK_COLON,     /* : */
    SC_TOK_SEMI,      /* ; */
    SC_TOK_COMMA,     /* , */
    SC_TOK_DOT,       /* . */

    /* operators */
    SC_TOK_ASSIGN,    /* = */
    SC_TOK_EQ,        /* == */
    SC_TOK_NEQ,       /* != */
    SC_TOK_LT,        /* < */
    SC_TOK_GT,        /* > */
    SC_TOK_LE,        /* <= */
    SC_TOK_GE,        /* >= */
    SC_TOK_PLUS,      /* + */
    SC_TOK_MINUS,     /* - */
    SC_TOK_STAR,      /* * */
    SC_TOK_SLASH,     /* / */
    SC_TOK_NOT,       /* ! */
    SC_TOK_AND_AND,   /* && */
    SC_TOK_OR_OR,     /* || */

    SC_TOK_ERROR
} sc_token_kind;

typedef struct sc_token {
    sc_token_kind kind;
    const char* start; /* points into source buffer, NOT nul-terminated */
    size_t length;
    double number_value; /* valid when kind == SC_TOK_NUMBER */
    int line;
    int column;
} sc_token;

typedef struct sc_lexer {
    const char* src;
    size_t length;
    size_t pos;
    int line;
    int column;
    char error_message[128];
    int has_error;
} sc_lexer;

void sc_lexer_init(sc_lexer* lexer, const char* src, size_t length);
sc_token sc_lexer_next(sc_lexer* lexer);
const char* sc_token_kind_name(sc_token_kind kind);

#endif /* BSHADER_LEXER_H */
