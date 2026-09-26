/* ast.h -- Abstract Syntax Tree node definitions for Bshader. Internal. */
#ifndef BSHADER_AST_H
#define BSHADER_AST_H

#include "arena.h"

typedef enum sc_property_type {
    SC_PROP_FLOAT,
    SC_PROP_INT,
    SC_PROP_BOOL,
    SC_PROP_VEC2,
    SC_PROP_VEC3,
    SC_PROP_VEC4,
    SC_PROP_TEXTURE2D
} sc_property_type;

typedef struct sc_property_decl {
    char* name;
    sc_property_type type;
    int has_default;
    /* Default value storage; only the field matching `type` is valid. */
    double default_number;   /* float/int */
    int default_bool;        /* bool */
    struct sc_property_decl* next;
} sc_property_decl;

/* ---- Expressions ---- */

typedef enum sc_expr_kind {
    SC_EXPR_NUMBER,
    SC_EXPR_BOOL,
    SC_EXPR_IDENTIFIER,
    SC_EXPR_CALL,
    SC_EXPR_BINARY,
    SC_EXPR_UNARY,
    SC_EXPR_MEMBER /* e.g. foo.rgb */
} sc_expr_kind;

typedef struct sc_expr sc_expr;

typedef struct sc_expr_list {
    sc_expr* expr;
    struct sc_expr_list* next;
} sc_expr_list;

struct sc_expr {
    sc_expr_kind kind;
    int line;

    /* SC_EXPR_NUMBER */
    double number_value;

    /* SC_EXPR_BOOL */
    int bool_value;

    /* SC_EXPR_IDENTIFIER / callee name / member base name */
    char* name;

    /* SC_EXPR_CALL */
    sc_expr_list* args;

    /* SC_EXPR_BINARY / SC_EXPR_UNARY */
    char op[3]; /* e.g. "+", "==", "&&" */
    sc_expr* left;
    sc_expr* right; /* unused for unary */

    /* SC_EXPR_MEMBER */
    sc_expr* object;
    char* member;
};

/* ---- Statements ---- */

typedef enum sc_stmt_kind {
    SC_STMT_VAR_DECL,   /* float x = expr; */
    SC_STMT_ASSIGN,     /* x = expr;  (also covers material output fields) */
    SC_STMT_IF,
    SC_STMT_EXPR
} sc_stmt_kind;

typedef struct sc_stmt sc_stmt;

typedef struct sc_stmt_list {
    sc_stmt* stmt;
    struct sc_stmt_list* next;
} sc_stmt_list;

struct sc_stmt {
    sc_stmt_kind kind;
    int line;

    /* SC_STMT_VAR_DECL */
    sc_property_type decl_type;
    char* decl_name;
    sc_expr* decl_init; /* may be NULL */

    /* SC_STMT_ASSIGN */
    char* assign_target;
    sc_expr* assign_value;

    /* SC_STMT_IF */
    sc_expr* if_cond;
    sc_stmt_list* if_then;
    sc_stmt_list* if_else; /* may be NULL */

    /* SC_STMT_EXPR */
    sc_expr* expr;
};

typedef struct sc_shader_decl {
    char* name;
    sc_property_decl* properties; /* linked list */
    sc_stmt_list* material_body;  /* linked list */
    int line;
} sc_shader_decl;

/* A Bshader source file currently contains exactly one `shader { }` block. */
typedef struct sc_program {
    sc_shader_decl* shader;
} sc_program;

/* ---- Constructors (arena-backed) ---- */

sc_expr* sc_ast_new_number(sc_arena* arena, double value, int line);
sc_expr* sc_ast_new_bool(sc_arena* arena, int value, int line);
sc_expr* sc_ast_new_identifier(sc_arena* arena, const char* name, size_t len, int line);
sc_expr* sc_ast_new_call(sc_arena* arena, const char* name, size_t len, sc_expr_list* args, int line);
sc_expr* sc_ast_new_binary(sc_arena* arena, const char* op, sc_expr* left, sc_expr* right, int line);
sc_expr* sc_ast_new_unary(sc_arena* arena, const char* op, sc_expr* operand, int line);
sc_expr* sc_ast_new_member(sc_arena* arena, sc_expr* object, const char* member, size_t len, int line);

sc_stmt* sc_ast_new_var_decl(sc_arena* arena, sc_property_type type, const char* name, size_t len, sc_expr* init, int line);
sc_stmt* sc_ast_new_assign(sc_arena* arena, const char* target, size_t len, sc_expr* value, int line);
sc_stmt* sc_ast_new_if(sc_arena* arena, sc_expr* cond, sc_stmt_list* then_body, sc_stmt_list* else_body, int line);

const char* sc_property_type_name(sc_property_type type);

#endif /* BSHADER_AST_H */
