#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef struct sc_keyword_entry {
    const char* text;
    sc_token_kind kind;
} sc_keyword_entry;

static const sc_keyword_entry SC_KEYWORDS[] = {
    {"shader", SC_TOK_KW_SHADER},
    {"properties", SC_TOK_KW_PROPERTIES},
    {"material", SC_TOK_KW_MATERIAL},
    {"if", SC_TOK_KW_IF},
    {"else", SC_TOK_KW_ELSE},
    {"true", SC_TOK_KW_TRUE},
    {"false", SC_TOK_KW_FALSE},
    {"float", SC_TOK_KW_FLOAT},
    {"int", SC_TOK_KW_INT},
    {"bool", SC_TOK_KW_BOOL},
    {"vec2", SC_TOK_KW_VEC2},
    {"vec3", SC_TOK_KW_VEC3},
    {"vec4", SC_TOK_KW_VEC4},
    {"texture2d", SC_TOK_KW_TEXTURE2D},
    {"mat3", SC_TOK_KW_MAT3}, {"mat4", SC_TOK_KW_MAT4},
    {"vertex", SC_TOK_KW_VERTEX}, {"options", SC_TOK_KW_OPTIONS},
    {"for", SC_TOK_KW_FOR}, {"while", SC_TOK_KW_WHILE},
    {"break", SC_TOK_KW_BREAK}, {"continue", SC_TOK_KW_CONTINUE},
};

void sc_lexer_init(sc_lexer* lexer, const char* src, size_t length) {
    lexer->src = src;
    lexer->length = length;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->error_message[0] = '\0';
    lexer->has_error = 0;
}

static int sc_at_end(const sc_lexer* lexer) {
    return lexer->pos >= lexer->length;
}

static char sc_peek(const sc_lexer* lexer) {
    return sc_at_end(lexer) ? '\0' : lexer->src[lexer->pos];
}

static char sc_peek_next(const sc_lexer* lexer) {
    return (lexer->pos + 1 >= lexer->length) ? '\0' : lexer->src[lexer->pos + 1];
}

static char sc_advance(sc_lexer* lexer) {
    char c = lexer->src[lexer->pos++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static void sc_skip_whitespace_and_comments(sc_lexer* lexer) {
    for (;;) {
        char c = sc_peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            sc_advance(lexer);
        } else if (c == '/' && sc_peek_next(lexer) == '/') {
            while (!sc_at_end(lexer) && sc_peek(lexer) != '\n') sc_advance(lexer);
        } else if (c == '/' && sc_peek_next(lexer) == '*') {
            int comment_line=lexer->line;
            sc_advance(lexer);
            sc_advance(lexer);
            while (!sc_at_end(lexer) && !(sc_peek(lexer) == '*' && sc_peek_next(lexer) == '/')) {
                sc_advance(lexer);
            }
            if (!sc_at_end(lexer)) {
                sc_advance(lexer);
                sc_advance(lexer);
            } else {
                lexer->has_error=1;
                snprintf(lexer->error_message,sizeof(lexer->error_message),"unterminated block comment at line %d",comment_line);
                return;
            }
        } else {
            break;
        }
    }
}

static sc_token sc_make_token(const sc_lexer* lexer, sc_token_kind kind,
                               const char* start, size_t length, int line, int col) {
    sc_token tok;
    tok.kind = kind;
    tok.start = start;
    tok.length = length;
    tok.number_value = 0.0;
    tok.line = line;
    tok.column = col;
    (void)lexer;
    return tok;
}

static sc_token_kind sc_keyword_lookup(const char* text, size_t len) {
    for (size_t i = 0; i < sizeof(SC_KEYWORDS) / sizeof(SC_KEYWORDS[0]); i++) {
        size_t klen = strlen(SC_KEYWORDS[i].text);
        if (klen == len && memcmp(SC_KEYWORDS[i].text, text, len) == 0) {
            return SC_KEYWORDS[i].kind;
        }
    }
    return SC_TOK_IDENTIFIER;
}

sc_token sc_lexer_next(sc_lexer* lexer) {
    lexer->has_error=0;
    sc_skip_whitespace_and_comments(lexer);

    int line = lexer->line;
    int col = lexer->column;
    if(lexer->has_error)return sc_make_token(lexer,SC_TOK_ERROR,lexer->src+lexer->pos,0,line,col);

    if (sc_at_end(lexer)) {
        return sc_make_token(lexer, SC_TOK_EOF, lexer->src + lexer->pos, 0, line, col);
    }

    const char* start = lexer->src + lexer->pos;
    char c = sc_advance(lexer);

    if (isalpha((unsigned char)c) || c == '_') {
        while (!sc_at_end(lexer) && (isalnum((unsigned char)sc_peek(lexer)) || sc_peek(lexer) == '_')) {
            sc_advance(lexer);
        }
        size_t len = (size_t)((lexer->src + lexer->pos) - start);
        sc_token_kind kind = sc_keyword_lookup(start, len);
        return sc_make_token(lexer, kind, start, len, line, col);
    }

    if (isdigit((unsigned char)c) || (c=='.' && isdigit((unsigned char)sc_peek(lexer)))) {
        while (!sc_at_end(lexer) && isdigit((unsigned char)sc_peek(lexer))) sc_advance(lexer);
        if (c!='.' && sc_peek(lexer) == '.') {
            sc_advance(lexer);
            while (!sc_at_end(lexer) && isdigit((unsigned char)sc_peek(lexer))) sc_advance(lexer);
        }
        if(sc_peek(lexer)=='e'||sc_peek(lexer)=='E'){
            sc_advance(lexer);
            if(sc_peek(lexer)=='+'||sc_peek(lexer)=='-')sc_advance(lexer);
            if(!isdigit((unsigned char)sc_peek(lexer))){snprintf(lexer->error_message,sizeof(lexer->error_message),"expected exponent digits");return sc_make_token(lexer,SC_TOK_ERROR,start,(size_t)(lexer->src+lexer->pos-start),line,col);}
            while(isdigit((unsigned char)sc_peek(lexer)))sc_advance(lexer);
        }
        size_t len = (size_t)((lexer->src + lexer->pos) - start);
        sc_token tok = sc_make_token(lexer, SC_TOK_NUMBER, start, len, line, col);
        char buf[64];
        if(len>=sizeof(buf)){snprintf(lexer->error_message,sizeof(lexer->error_message),"numeric literal too long");return sc_make_token(lexer,SC_TOK_ERROR,start,len,line,col);}
        size_t copy_len = len;
        memcpy(buf, start, copy_len);
        buf[copy_len] = '\0';
        tok.number_value = strtod(buf,NULL);
        if(!isfinite(tok.number_value)){snprintf(lexer->error_message,sizeof(lexer->error_message),"numeric literal must be finite");tok.kind=SC_TOK_ERROR;}
        return tok;
    }

    if (c == '"') {
        while (!sc_at_end(lexer) && sc_peek(lexer) != '"') {
            if (sc_peek(lexer) == '\n') break;
            sc_advance(lexer);
        }
        const char* str_start = start + 1;
        size_t str_len = (size_t)((lexer->src + lexer->pos) - str_start);
        if (sc_peek(lexer) == '"') {
            sc_advance(lexer);
        } else {
            lexer->has_error = 1;
            snprintf(lexer->error_message, sizeof(lexer->error_message),
                     "unterminated string literal at line %d", line);
            return sc_make_token(lexer,SC_TOK_ERROR,str_start,str_len,line,col);
        }
        return sc_make_token(lexer, SC_TOK_STRING, str_start, str_len, line, col);
    }

    switch (c) {
        case '{': return sc_make_token(lexer, SC_TOK_LBRACE, start, 1, line, col);
        case '}': return sc_make_token(lexer, SC_TOK_RBRACE, start, 1, line, col);
        case '(': return sc_make_token(lexer, SC_TOK_LPAREN, start, 1, line, col);
        case ')': return sc_make_token(lexer, SC_TOK_RPAREN, start, 1, line, col);
        case ':': return sc_make_token(lexer, SC_TOK_COLON, start, 1, line, col);
        case ';': return sc_make_token(lexer, SC_TOK_SEMI, start, 1, line, col);
        case ',': return sc_make_token(lexer, SC_TOK_COMMA, start, 1, line, col);
        case '.': return sc_make_token(lexer, SC_TOK_DOT, start, 1, line, col);
        case '[': return sc_make_token(lexer, SC_TOK_LBRACKET, start, 1, line, col);
        case ']': return sc_make_token(lexer, SC_TOK_RBRACKET, start, 1, line, col);
        case '+': if(sc_peek(lexer)=='+'){sc_advance(lexer);return sc_make_token(lexer,SC_TOK_PLUS_PLUS,start,2,line,col);}return sc_make_token(lexer, SC_TOK_PLUS, start, 1, line, col);
        case '-': if(sc_peek(lexer)=='-'){sc_advance(lexer);return sc_make_token(lexer,SC_TOK_MINUS_MINUS,start,2,line,col);}return sc_make_token(lexer, SC_TOK_MINUS, start, 1, line, col);
        case '*': return sc_make_token(lexer, SC_TOK_STAR, start, 1, line, col);
        case '/': return sc_make_token(lexer, SC_TOK_SLASH, start, 1, line, col);
        case '=':
            if (sc_peek(lexer) == '=') { sc_advance(lexer); return sc_make_token(lexer, SC_TOK_EQ, start, 2, line, col); }
            return sc_make_token(lexer, SC_TOK_ASSIGN, start, 1, line, col);
        case '!':
            if (sc_peek(lexer) == '=') { sc_advance(lexer); return sc_make_token(lexer, SC_TOK_NEQ, start, 2, line, col); }
            return sc_make_token(lexer, SC_TOK_NOT, start, 1, line, col);
        case '<':
            if (sc_peek(lexer) == '=') { sc_advance(lexer); return sc_make_token(lexer, SC_TOK_LE, start, 2, line, col); }
            return sc_make_token(lexer, SC_TOK_LT, start, 1, line, col);
        case '>':
            if (sc_peek(lexer) == '=') { sc_advance(lexer); return sc_make_token(lexer, SC_TOK_GE, start, 2, line, col); }
            return sc_make_token(lexer, SC_TOK_GT, start, 1, line, col);
        case '&':
            if (sc_peek(lexer) == '&') { sc_advance(lexer); return sc_make_token(lexer, SC_TOK_AND_AND, start, 2, line, col); }
            break;
        case '|':
            if (sc_peek(lexer) == '|') { sc_advance(lexer); return sc_make_token(lexer, SC_TOK_OR_OR, start, 2, line, col); }
            break;
        default:
            break;
    }

    lexer->has_error = 1;
    snprintf(lexer->error_message, sizeof(lexer->error_message),
             "unexpected character '%c' at line %d, column %d", c, line, col);
    return sc_make_token(lexer, SC_TOK_ERROR, start, 1, line, col);
}

const char* sc_token_kind_name(sc_token_kind kind) {
    switch (kind) {
        case SC_TOK_EOF: return "eof";
        case SC_TOK_IDENTIFIER: return "identifier";
        case SC_TOK_NUMBER: return "number";
        case SC_TOK_STRING: return "string";
        case SC_TOK_KW_SHADER: return "'shader'";
        case SC_TOK_KW_PROPERTIES: return "'properties'";
        case SC_TOK_KW_MATERIAL: return "'material'";
        case SC_TOK_KW_IF: return "'if'";
        case SC_TOK_KW_ELSE: return "'else'";
        case SC_TOK_KW_TRUE: return "'true'";
        case SC_TOK_KW_FALSE: return "'false'";
        case SC_TOK_KW_FLOAT: return "'float'";
        case SC_TOK_KW_INT: return "'int'";
        case SC_TOK_KW_BOOL: return "'bool'";
        case SC_TOK_KW_VEC2: return "'vec2'";
        case SC_TOK_KW_VEC3: return "'vec3'";
        case SC_TOK_KW_VEC4: return "'vec4'";
        case SC_TOK_KW_TEXTURE2D: return "'texture2d'";
        case SC_TOK_KW_MAT3: return "'mat3'";
        case SC_TOK_KW_MAT4: return "'mat4'";
        case SC_TOK_KW_VERTEX: return "'vertex'";
        case SC_TOK_KW_OPTIONS: return "'options'";
        case SC_TOK_KW_FOR: return "'for'";
        case SC_TOK_KW_WHILE: return "'while'";
        case SC_TOK_KW_BREAK: return "'break'";
        case SC_TOK_KW_CONTINUE: return "'continue'";
        case SC_TOK_LBRACKET: return "'['";
        case SC_TOK_RBRACKET: return "']'";
        case SC_TOK_PLUS_PLUS: return "'++'";
        case SC_TOK_MINUS_MINUS: return "'--'";
        case SC_TOK_LBRACE: return "'{'";
        case SC_TOK_RBRACE: return "'}'";
        case SC_TOK_LPAREN: return "'('";
        case SC_TOK_RPAREN: return "')'";
        case SC_TOK_COLON: return "':'";
        case SC_TOK_SEMI: return "';'";
        case SC_TOK_COMMA: return "','";
        case SC_TOK_DOT: return "'.'";
        case SC_TOK_ASSIGN: return "'='";
        case SC_TOK_EQ: return "'=='";
        case SC_TOK_NEQ: return "'!='";
        case SC_TOK_LT: return "'<'";
        case SC_TOK_GT: return "'>'";
        case SC_TOK_LE: return "'<='";
        case SC_TOK_GE: return "'>='";
        case SC_TOK_PLUS: return "'+'";
        case SC_TOK_MINUS: return "'-'";
        case SC_TOK_STAR: return "'*'";
        case SC_TOK_SLASH: return "'/'";
        case SC_TOK_NOT: return "'!'";
        case SC_TOK_AND_AND: return "'&&'";
        case SC_TOK_OR_OR: return "'||'";
        case SC_TOK_ERROR: return "<error>";
        default: return "<unknown>";
    }
}
