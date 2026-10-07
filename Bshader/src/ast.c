#include "ast.h"
#include <string.h>

sc_expr* sc_ast_new_number(sc_arena* arena, double value, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_NUMBER;
    e->number_value = value;
    e->line = line;
    return e;
}

sc_expr* sc_ast_new_bool(sc_arena* arena, int value, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_BOOL;
    e->bool_value = value;
    e->line = line;
    return e;
}

sc_expr* sc_ast_new_identifier(sc_arena* arena, const char* name, size_t len, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_IDENTIFIER;
    e->name = sc_arena_strndup(arena, name, len);
    e->line = line;
    return e;
}

sc_expr* sc_ast_new_call(sc_arena* arena, const char* name, size_t len, sc_expr_list* args, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_CALL;
    e->name = sc_arena_strndup(arena, name, len);
    e->args = args;
    e->line = line;
    return e;
}

sc_expr* sc_ast_new_binary(sc_arena* arena, const char* op, sc_expr* left, sc_expr* right, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_BINARY;
    strncpy(e->op, op, sizeof(e->op) - 1);
    e->left = left;
    e->right = right;
    e->line = line;
    return e;
}

sc_expr* sc_ast_new_unary(sc_arena* arena, const char* op, sc_expr* operand, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_UNARY;
    strncpy(e->op, op, sizeof(e->op) - 1);
    e->left = operand;
    e->line = line;
    return e;
}

sc_expr* sc_ast_new_member(sc_arena* arena, sc_expr* object, const char* member, size_t len, int line) {
    sc_expr* e = (sc_expr*)sc_arena_alloc_zeroed(arena, sizeof(sc_expr));
    e->kind = SC_EXPR_MEMBER;
    e->object = object;
    e->member = sc_arena_strndup(arena, member, len);
    e->line = line;
    return e;
}

sc_stmt* sc_ast_new_var_decl(sc_arena* arena, sc_property_type type, const char* name, size_t len, sc_expr* init, int line) {
    sc_stmt* s = (sc_stmt*)sc_arena_alloc_zeroed(arena, sizeof(sc_stmt));
    s->kind = SC_STMT_VAR_DECL;
    s->decl_type = type;
    s->decl_name = sc_arena_strndup(arena, name, len);
    s->decl_init = init;
    s->line = line;
    return s;
}

sc_stmt* sc_ast_new_assign(sc_arena* arena, const char* target, size_t len, sc_expr* value, int line) {
    sc_stmt* s = (sc_stmt*)sc_arena_alloc_zeroed(arena, sizeof(sc_stmt));
    s->kind = SC_STMT_ASSIGN;
    s->assign_target = sc_arena_strndup(arena, target, len);
    s->assign_value = value;
    s->line = line;
    return s;
}

sc_stmt* sc_ast_new_if(sc_arena* arena, sc_expr* cond, sc_stmt_list* then_body, sc_stmt_list* else_body, int line) {
    sc_stmt* s = (sc_stmt*)sc_arena_alloc_zeroed(arena, sizeof(sc_stmt));
    s->kind = SC_STMT_IF;
    s->if_cond = cond;
    s->if_then = then_body;
    s->if_else = else_body;
    s->line = line;
    return s;
}

const char* sc_property_type_name(sc_property_type type) {
    switch (type) {
        case SC_PROP_FLOAT: return "float";
        case SC_PROP_INT: return "int";
        case SC_PROP_BOOL: return "bool";
        case SC_PROP_VEC2: return "vec2";
        case SC_PROP_VEC3: return "vec3";
        case SC_PROP_VEC4: return "vec4";
        case SC_PROP_TEXTURE2D: return "texture2d";
        case SC_PROP_MAT3: return "mat3";
        case SC_PROP_MAT4: return "mat4";
        default: return "unknown";
    }
}
