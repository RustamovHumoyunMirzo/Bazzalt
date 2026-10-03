#include "parser.h"
#include "lexer.h"
#include <string.h>
#include <stdio.h>

typedef struct sc_parser {
    sc_lexer lexer;
    sc_arena* arena;
    sc_diag_list* diags;

    sc_token current;
    sc_token previous;
    int panic_mode;
} sc_parser;

/* ---- token stream helpers ---- */

static void sc_advance_token(sc_parser* p) {
    p->previous = p->current;
    for (;;) {
        p->current = sc_lexer_next(&p->lexer);
        if (p->current.kind != SC_TOK_ERROR) break;
        sc_diag_add(p->diags, SC_DIAG_ERROR, p->current.line, p->current.column,
                    "%s", p->lexer.error_message[0] ? p->lexer.error_message : "invalid token");
    }
}

static int sc_check(sc_parser* p, sc_token_kind kind) {
    return p->current.kind == kind;
}

static int sc_match(sc_parser* p, sc_token_kind kind) {
    if (!sc_check(p, kind)) return 0;
    sc_advance_token(p);
    return 1;
}

static void sc_error_at_current(sc_parser* p, const char* message) {
    sc_diag_add(p->diags, SC_DIAG_ERROR, p->current.line, p->current.column, "%s", message);
    p->panic_mode = 1;
}

static int sc_expect(sc_parser* p, sc_token_kind kind, const char* what) {
    if (sc_check(p, kind)) {
        sc_advance_token(p);
        return 1;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "expected %s but found %s", what, sc_token_kind_name(p->current.kind));
    sc_error_at_current(p, buf);
    return 0;
}

/* Skip tokens until we reach a plausible statement/declaration boundary,
 * so a single syntax error doesn't cascade into dozens of diagnostics. */
static void sc_synchronize(sc_parser* p) {
    p->panic_mode = 0;
    while (!sc_check(p, SC_TOK_EOF)) {
        if (p->previous.kind == SC_TOK_SEMI || p->previous.kind == SC_TOK_RBRACE) return;
        switch (p->current.kind) {
            case SC_TOK_KW_SHADER:
            case SC_TOK_KW_PROPERTIES:
            case SC_TOK_KW_MATERIAL:
            case SC_TOK_KW_IF:
            case SC_TOK_RBRACE:
                return;
            default:
                break;
        }
        sc_advance_token(p);
    }
}

/* ---- type parsing ---- */

static int sc_token_is_type_keyword(sc_token_kind kind) {
    switch (kind) {
        case SC_TOK_KW_FLOAT:
        case SC_TOK_KW_INT:
        case SC_TOK_KW_BOOL:
        case SC_TOK_KW_VEC2:
        case SC_TOK_KW_VEC3:
        case SC_TOK_KW_VEC4:
        case SC_TOK_KW_TEXTURE2D:
            return 1;
        default:
            return 0;
    }
}

static sc_property_type sc_type_from_token(sc_token_kind kind) {
    switch (kind) {
        case SC_TOK_KW_FLOAT: return SC_PROP_FLOAT;
        case SC_TOK_KW_INT: return SC_PROP_INT;
        case SC_TOK_KW_BOOL: return SC_PROP_BOOL;
        case SC_TOK_KW_VEC2: return SC_PROP_VEC2;
        case SC_TOK_KW_VEC3: return SC_PROP_VEC3;
        case SC_TOK_KW_VEC4: return SC_PROP_VEC4;
        case SC_TOK_KW_TEXTURE2D: return SC_PROP_TEXTURE2D;
        default: return SC_PROP_FLOAT;
    }
}

/* ---- expression parsing (precedence climbing) ---- */

static sc_expr* sc_parse_expr(sc_parser* p);

static sc_expr_list* sc_parse_arg_list(sc_parser* p) {
    sc_expr_list* head = NULL;
    sc_expr_list** tail = &head;

    if (!sc_check(p, SC_TOK_RPAREN)) {
        do {
            sc_expr* arg = sc_parse_expr(p);
            sc_expr_list* node = (sc_expr_list*)sc_arena_alloc_zeroed(p->arena, sizeof(sc_expr_list));
            node->expr = arg;
            *tail = node;
            tail = &node->next;
        } while (sc_match(p, SC_TOK_COMMA));
    }
    sc_expect(p, SC_TOK_RPAREN, "')' after argument list");
    return head;
}

static sc_expr* sc_parse_primary(sc_parser* p) {
    int line = p->current.line;

    if (sc_match(p, SC_TOK_NUMBER)) {
        return sc_ast_new_number(p->arena, p->previous.number_value, line);
    }
    if (sc_match(p, SC_TOK_KW_TRUE)) {
        return sc_ast_new_bool(p->arena, 1, line);
    }
    if (sc_match(p, SC_TOK_KW_FALSE)) {
        return sc_ast_new_bool(p->arena, 0, line);
    }
    if (sc_match(p, SC_TOK_LPAREN)) {
        sc_expr* inner = sc_parse_expr(p);
        sc_expect(p, SC_TOK_RPAREN, "')' after expression");
        return inner;
    }
    if (sc_check(p, SC_TOK_IDENTIFIER) || sc_check(p, SC_TOK_KW_FLOAT) ||
        sc_check(p, SC_TOK_KW_INT) || sc_check(p, SC_TOK_KW_BOOL) ||
        sc_check(p, SC_TOK_KW_VEC2) || sc_check(p, SC_TOK_KW_VEC3) ||
        sc_check(p, SC_TOK_KW_VEC4)) {
        sc_token id = p->current;
        sc_advance_token(p);
        if (sc_match(p, SC_TOK_LPAREN)) {
            sc_expr_list* args = sc_parse_arg_list(p);
            return sc_ast_new_call(p->arena, id.start, id.length, args, line);
        }
        return sc_ast_new_identifier(p->arena, id.start, id.length, line);
    }

    sc_error_at_current(p, "expected an expression");
    /* Return a harmless placeholder so the caller can keep walking. */
    return sc_ast_new_number(p->arena, 0.0, line);
}

static sc_expr* sc_parse_postfix(sc_parser* p) {
    sc_expr* expr = sc_parse_primary(p);
    while (sc_check(p, SC_TOK_DOT)) {
        int line = p->current.line;
        sc_advance_token(p);
        if (!sc_check(p, SC_TOK_IDENTIFIER)) {
            sc_error_at_current(p, "expected a member name after '.'");
            break;
        }
        sc_token member = p->current;
        sc_advance_token(p);
        expr = sc_ast_new_member(p->arena, expr, member.start, member.length, line);
    }
    return expr;
}

static sc_expr* sc_parse_unary(sc_parser* p) {
    if (sc_check(p, SC_TOK_MINUS) || sc_check(p, SC_TOK_NOT)) {
        sc_token op = p->current;
        int line = op.line;
        sc_advance_token(p);
        sc_expr* operand = sc_parse_unary(p);
        char op_str[3] = {0};
        op_str[0] = (op.kind == SC_TOK_MINUS) ? '-' : '!';
        return sc_ast_new_unary(p->arena, op_str, operand, line);
    }
    return sc_parse_postfix(p);
}

static sc_expr* sc_parse_multiplicative(sc_parser* p) {
    sc_expr* left = sc_parse_unary(p);
    while (sc_check(p, SC_TOK_STAR) || sc_check(p, SC_TOK_SLASH)) {
        int line = p->current.line;
        const char* op = sc_check(p, SC_TOK_STAR) ? "*" : "/";
        sc_advance_token(p);
        sc_expr* right = sc_parse_unary(p);
        left = sc_ast_new_binary(p->arena, op, left, right, line);
    }
    return left;
}

static sc_expr* sc_parse_additive(sc_parser* p) {
    sc_expr* left = sc_parse_multiplicative(p);
    while (sc_check(p, SC_TOK_PLUS) || sc_check(p, SC_TOK_MINUS)) {
        int line = p->current.line;
        const char* op = sc_check(p, SC_TOK_PLUS) ? "+" : "-";
        sc_advance_token(p);
        sc_expr* right = sc_parse_multiplicative(p);
        left = sc_ast_new_binary(p->arena, op, left, right, line);
    }
    return left;
}

static sc_expr* sc_parse_relational(sc_parser* p) {
    sc_expr* left = sc_parse_additive(p);
    while (sc_check(p, SC_TOK_LT) || sc_check(p, SC_TOK_GT) ||
           sc_check(p, SC_TOK_LE) || sc_check(p, SC_TOK_GE)) {
        int line = p->current.line;
        const char* op = sc_check(p, SC_TOK_LT) ? "<" :
                          sc_check(p, SC_TOK_GT) ? ">" :
                          sc_check(p, SC_TOK_LE) ? "<=" : ">=";
        sc_advance_token(p);
        sc_expr* right = sc_parse_additive(p);
        left = sc_ast_new_binary(p->arena, op, left, right, line);
    }
    return left;
}

static sc_expr* sc_parse_equality(sc_parser* p) {
    sc_expr* left = sc_parse_relational(p);
    while (sc_check(p, SC_TOK_EQ) || sc_check(p, SC_TOK_NEQ)) {
        int line = p->current.line;
        const char* op = sc_check(p, SC_TOK_EQ) ? "==" : "!=";
        sc_advance_token(p);
        sc_expr* right = sc_parse_relational(p);
        left = sc_ast_new_binary(p->arena, op, left, right, line);
    }
    return left;
}

static sc_expr* sc_parse_logical_and(sc_parser* p) {
    sc_expr* left = sc_parse_equality(p);
    while (sc_check(p, SC_TOK_AND_AND)) {
        int line = p->current.line;
        sc_advance_token(p);
        sc_expr* right = sc_parse_equality(p);
        left = sc_ast_new_binary(p->arena, "&&", left, right, line);
    }
    return left;
}

static sc_expr* sc_parse_logical_or(sc_parser* p) {
    sc_expr* left = sc_parse_logical_and(p);
    while (sc_check(p, SC_TOK_OR_OR)) {
        int line = p->current.line;
        sc_advance_token(p);
        sc_expr* right = sc_parse_logical_and(p);
        left = sc_ast_new_binary(p->arena, "||", left, right, line);
    }
    return left;
}

static sc_expr* sc_parse_expr(sc_parser* p) {
    return sc_parse_logical_or(p);
}

/* ---- statement parsing ---- */

static sc_stmt_list* sc_parse_stmt_block(sc_parser* p); /* forward */

static sc_stmt* sc_parse_if_stmt(sc_parser* p) {
    int line = p->previous.line; /* 'if' already consumed by caller */
    sc_expect(p, SC_TOK_LPAREN, "'(' after 'if'");
    sc_expr* cond = sc_parse_expr(p);
    sc_expect(p, SC_TOK_RPAREN, "')' after if condition");
    sc_stmt_list* then_body = sc_parse_stmt_block(p);
    sc_stmt_list* else_body = NULL;
    if (sc_match(p, SC_TOK_KW_ELSE)) {
        if (sc_check(p, SC_TOK_KW_IF)) {
            sc_advance_token(p);
            sc_stmt* nested = sc_parse_if_stmt(p);
            sc_stmt_list* node = (sc_stmt_list*)sc_arena_alloc_zeroed(p->arena, sizeof(sc_stmt_list));
            node->stmt = nested;
            else_body = node;
        } else {
            else_body = sc_parse_stmt_block(p);
        }
    }
    return sc_ast_new_if(p->arena, cond, then_body, else_body, line);
}

static sc_stmt* sc_parse_stmt(sc_parser* p) {
    int line = p->current.line;

    if (sc_match(p, SC_TOK_KW_IF)) {
        return sc_parse_if_stmt(p);
    }

    if (sc_token_is_type_keyword(p->current.kind)) {
        sc_property_type type = sc_type_from_token(p->current.kind);
        sc_advance_token(p);
        if (!sc_check(p, SC_TOK_IDENTIFIER)) {
            sc_error_at_current(p, "expected a variable name after type");
            return NULL;
        }
        sc_token name = p->current;
        sc_advance_token(p);
        sc_expr* init = NULL;
        if (sc_match(p, SC_TOK_ASSIGN)) {
            init = sc_parse_expr(p);
        }
        sc_expect(p, SC_TOK_SEMI, "';' after variable declaration");
        return sc_ast_new_var_decl(p->arena, type, name.start, name.length, init, line);
    }

    if (sc_check(p, SC_TOK_IDENTIFIER)) {
        sc_token name = p->current;
        sc_advance_token(p);
        sc_expect(p, SC_TOK_ASSIGN, "'=' after assignment target");
        sc_expr* value = sc_parse_expr(p);
        sc_expect(p, SC_TOK_SEMI, "';' after assignment");
        return sc_ast_new_assign(p->arena, name.start, name.length, value, line);
    }

    sc_error_at_current(p, "expected a statement (variable declaration, assignment, or 'if')");
    /* consume one token to guarantee forward progress before synchronizing */
    if (!sc_check(p, SC_TOK_EOF) && !sc_check(p, SC_TOK_RBRACE)) sc_advance_token(p);
    return NULL;
}

static sc_stmt_list* sc_parse_stmt_block(sc_parser* p) {
    sc_expect(p, SC_TOK_LBRACE, "'{' to start a block");
    sc_stmt_list* head = NULL;
    sc_stmt_list** tail = &head;

    while (!sc_check(p, SC_TOK_RBRACE) && !sc_check(p, SC_TOK_EOF)) {
        sc_stmt* stmt = sc_parse_stmt(p);
        if (p->panic_mode) {
            sc_synchronize(p);
        }
        if (stmt) {
            sc_stmt_list* node = (sc_stmt_list*)sc_arena_alloc_zeroed(p->arena, sizeof(sc_stmt_list));
            node->stmt = stmt;
            *tail = node;
            tail = &node->next;
        }
    }
    sc_expect(p, SC_TOK_RBRACE, "'}' to close a block");
    return head;
}

/* ---- top-level: properties { } and material { } ---- */

static sc_property_decl* sc_parse_properties_block(sc_parser* p) {
    sc_expect(p, SC_TOK_LBRACE, "'{' after 'properties'");
    sc_property_decl* head = NULL;
    sc_property_decl** tail = &head;

    while (!sc_check(p, SC_TOK_RBRACE) && !sc_check(p, SC_TOK_EOF)) {
        if (!sc_check(p, SC_TOK_IDENTIFIER)) {
            sc_error_at_current(p, "expected a property name");
            sc_synchronize(p);
            continue;
        }
        sc_token name = p->current;
        sc_advance_token(p);
        sc_expect(p, SC_TOK_COLON, "':' after property name");

        if (!sc_token_is_type_keyword(p->current.kind)) {
            sc_error_at_current(p, "expected a property type (float, int, bool, vec2, vec3, vec4, texture2d)");
            sc_synchronize(p);
            continue;
        }
        sc_property_type type = sc_type_from_token(p->current.kind);
        sc_advance_token(p);

        sc_property_decl* decl = (sc_property_decl*)sc_arena_alloc_zeroed(p->arena, sizeof(sc_property_decl));
        decl->name = sc_arena_strndup(p->arena, name.start, name.length);
        decl->type = type;
        decl->has_default = 0;

        if (sc_match(p, SC_TOK_ASSIGN)) {
            decl->has_default = 1;
            if (type == SC_PROP_BOOL) {
                if (sc_match(p, SC_TOK_KW_TRUE)) decl->default_bool = 1;
                else if (sc_match(p, SC_TOK_KW_FALSE)) decl->default_bool = 0;
                else sc_error_at_current(p, "expected 'true' or 'false' for bool default");
            } else if (type == SC_PROP_TEXTURE2D) {
                sc_error_at_current(p, "texture2d properties cannot have a default value");
            } else {
                int neg = sc_match(p, SC_TOK_MINUS);
                if (sc_check(p, SC_TOK_NUMBER)) {
                    decl->default_number = neg ? -p->current.number_value : p->current.number_value;
                    sc_advance_token(p);
                } else {
                    sc_error_at_current(p, "expected a numeric default value");
                }
            }
        }

        sc_expect(p, SC_TOK_SEMI, "';' after property declaration");

        *tail = decl;
        tail = &decl->next;
    }
    sc_expect(p, SC_TOK_RBRACE, "'}' to close properties block");
    return head;
}

static sc_stmt_list* sc_parse_material_block(sc_parser* p) {
    return sc_parse_stmt_block(p);
}

static sc_shader_decl* sc_parse_shader_decl(sc_parser* p) {
    int line = p->current.line;
    sc_expect(p, SC_TOK_KW_SHADER, "'shader'");

    if (!sc_check(p, SC_TOK_IDENTIFIER)) {
        sc_error_at_current(p, "expected a shader name after 'shader'");
        return NULL;
    }
    sc_token name = p->current;
    sc_advance_token(p);

    sc_expect(p, SC_TOK_LBRACE, "'{' to start shader body");

    sc_shader_decl* decl = (sc_shader_decl*)sc_arena_alloc_zeroed(p->arena, sizeof(sc_shader_decl));
    decl->name = sc_arena_strndup(p->arena, name.start, name.length);
    decl->line = line;

    int saw_properties = 0;
    int saw_material = 0;

    while (!sc_check(p, SC_TOK_RBRACE) && !sc_check(p, SC_TOK_EOF)) {
        if (sc_check(p, SC_TOK_KW_PROPERTIES)) {
            sc_advance_token(p);
            if (saw_properties) {
                sc_diag_add(p->diags, SC_DIAG_ERROR, p->previous.line, p->previous.column,
                            "duplicate 'properties' block");
            }
            decl->properties = sc_parse_properties_block(p);
            saw_properties = 1;
        } else if (sc_check(p, SC_TOK_KW_MATERIAL)) {
            sc_advance_token(p);
            if (saw_material) {
                sc_diag_add(p->diags, SC_DIAG_ERROR, p->previous.line, p->previous.column,
                            "duplicate 'material' block");
            }
            decl->material_body = sc_parse_material_block(p);
            saw_material = 1;
        } else {
            sc_error_at_current(p, "expected 'properties' or 'material' block inside shader");
            sc_synchronize(p);
        }
    }
    sc_expect(p, SC_TOK_RBRACE, "'}' to close shader body");

    if (!saw_material) {
        sc_diag_add(p->diags, SC_DIAG_ERROR, decl->line, 1,
                    "shader '%s' has no 'material' block", decl->name);
    }

    return decl;
}

sc_program* sc_parse_program(sc_arena* arena, sc_diag_list* diags, const char* src, size_t length) {
    sc_parser parser;
    sc_lexer_init(&parser.lexer, src, length);
    parser.arena = arena;
    parser.diags = diags;
    parser.panic_mode = 0;
    memset(&parser.previous, 0, sizeof(parser.previous));
    parser.current = sc_lexer_next(&parser.lexer);

    sc_program* program = (sc_program*)sc_arena_alloc_zeroed(arena, sizeof(sc_program));

    if (sc_check(&parser, SC_TOK_EOF)) {
        sc_diag_add(diags, SC_DIAG_ERROR, 1, 1, "empty source: expected a 'shader' declaration");
        return program;
    }

    if (!sc_check(&parser, SC_TOK_KW_SHADER)) {
        sc_error_at_current(&parser, "expected source to begin with a 'shader' declaration");
        sc_synchronize(&parser);
    }

    if (sc_check(&parser, SC_TOK_KW_SHADER)) {
        program->shader = sc_parse_shader_decl(&parser);
    }

    if (!sc_check(&parser, SC_TOK_EOF)) {
        sc_diag_add(diags, SC_DIAG_WARNING, parser.current.line, parser.current.column,
                    "unexpected trailing content after shader declaration; ignoring rest of file");
    }

    return program;
}
