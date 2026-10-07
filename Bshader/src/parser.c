#include "parser.h"
#include "lexer.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

typedef struct sc_parser {
    sc_lexer lexer;
    sc_arena* arena;
    sc_diag_list* diags;

    sc_token current;
    sc_token previous;
    int panic_mode;
    int loop_depth;
    int depth;
    unsigned int tokens;
} sc_parser;

/* ---- token stream helpers ---- */

static void sc_advance_token(sc_parser* p) {
    if(++p->tokens>100000){sc_diag_add(p->diags,SC_DIAG_ERROR,p->current.line,p->current.column,"source token limit exceeded");p->current.kind=SC_TOK_EOF;return;}
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
        case SC_TOK_KW_MAT3:
        case SC_TOK_KW_MAT4:
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
        case SC_TOK_KW_MAT3: return SC_PROP_MAT3;
        case SC_TOK_KW_MAT4: return SC_PROP_MAT4;
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
        sc_expr* number=sc_ast_new_number(p->arena, p->previous.number_value, line);
        number->name=sc_arena_strndup(p->arena,p->previous.start,p->previous.length);
        for(size_t i=0;i<p->previous.length;++i)if(p->previous.start[i]=='.'||p->previous.start[i]=='e'||p->previous.start[i]=='E')number->number_is_float=1;
        return number;
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
        sc_check(p, SC_TOK_KW_VEC4) || sc_check(p,SC_TOK_KW_MAT3) || sc_check(p,SC_TOK_KW_MAT4)) {
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
    while (sc_check(p, SC_TOK_DOT) || sc_check(p,SC_TOK_LBRACKET)) {
        int line = p->current.line;
        if(sc_match(p,SC_TOK_LBRACKET)){
            sc_expr* index=sc_parse_expr(p);sc_expect(p,SC_TOK_RBRACKET,"']' after index");
            sc_expr* node=(sc_expr*)sc_arena_alloc_zeroed(p->arena,sizeof(sc_expr));node->kind=SC_EXPR_INDEX;node->object=expr;node->right=index;node->line=line;expr=node;continue;
        }
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
        if(++p->depth>128){sc_error_at_current(p,"expression nesting limit exceeded");p->current.kind=SC_TOK_EOF;--p->depth;return sc_ast_new_number(p->arena,0,line);}
        sc_expr* operand = sc_parse_unary(p);
        --p->depth;
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
    if(++p->depth>128){sc_error_at_current(p,"expression nesting limit exceeded");p->current.kind=SC_TOK_EOF;--p->depth;return sc_ast_new_number(p->arena,0,p->current.line);}
    sc_expr* result=sc_parse_logical_or(p);--p->depth;return result;
}

/* ---- statement parsing ---- */

static sc_stmt_list* sc_parse_stmt_block(sc_parser* p); /* forward */
static sc_stmt* sc_parse_stmt(sc_parser* p);

static sc_stmt* sc_parse_assignment(sc_parser* p,int semi){
    int line=p->current.line;
    sc_expr* target=sc_parse_postfix(p);
    sc_expr* value=NULL;
    if(sc_check(p,SC_TOK_PLUS_PLUS)||sc_check(p,SC_TOK_MINUS_MINUS)){
        const char* op=sc_check(p,SC_TOK_PLUS_PLUS)?"+":"-";sc_advance_token(p);
        value=sc_ast_new_binary(p->arena,op,target,sc_ast_new_number(p->arena,1,line),line);
    }else{sc_expect(p,SC_TOK_ASSIGN,"'=' after assignment target");value=sc_parse_expr(p);}
    if(semi)sc_expect(p,SC_TOK_SEMI,"';' after assignment");
    sc_stmt* stmt=sc_ast_new_assign(p->arena,"",0,value,line);stmt->assign_lvalue=target;
    return stmt;
}

static sc_stmt* sc_parse_loop(sc_parser* p,int is_for){
    int line=p->previous.line;
    sc_stmt* stmt=(sc_stmt*)sc_arena_alloc_zeroed(p->arena,sizeof(sc_stmt));stmt->kind=is_for?SC_STMT_FOR:SC_STMT_WHILE;stmt->line=line;
    sc_expect(p,SC_TOK_LPAREN,"'(' after loop keyword");
    if(is_for){
        if(sc_check(p,SC_TOK_KW_INT)){stmt->loop_init=sc_parse_stmt(p);if(!stmt->loop_init||!stmt->loop_init->decl_init)sc_error_at_current(p,"for counter requires an initializer");}
        else {sc_error_at_current(p,"for initialization must declare an int counter");return stmt;}
    }
    stmt->loop_cond=sc_parse_expr(p);
    if(is_for){sc_expect(p,SC_TOK_SEMI,"';' after for condition");stmt->loop_step=sc_parse_assignment(p,0);}
    sc_expect(p,SC_TOK_RPAREN,"')' after loop header");
    ++p->loop_depth;stmt->loop_body=sc_parse_stmt_block(p);--p->loop_depth;
    return stmt;
}

static sc_stmt* sc_parse_if_stmt(sc_parser* p) {
    if(++p->depth>128){sc_error_at_current(p,"branch nesting limit exceeded");p->current.kind=SC_TOK_EOF;--p->depth;return NULL;}
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
    --p->depth;
    return sc_ast_new_if(p->arena, cond, then_body, else_body, line);
}

static sc_stmt* sc_parse_stmt(sc_parser* p) {
    int line = p->current.line;
    if(sc_match(p,SC_TOK_KW_FOR))return sc_parse_loop(p,1);
    if(sc_match(p,SC_TOK_KW_WHILE))return sc_parse_loop(p,0);
    if(sc_check(p,SC_TOK_KW_BREAK)||sc_check(p,SC_TOK_KW_CONTINUE)){
        sc_stmt* stmt=(sc_stmt*)sc_arena_alloc_zeroed(p->arena,sizeof(sc_stmt));stmt->kind=sc_check(p,SC_TOK_KW_BREAK)?SC_STMT_BREAK:SC_STMT_CONTINUE;stmt->line=line;
        if(!p->loop_depth)sc_error_at_current(p,"break/continue requires a loop");
        sc_advance_token(p);sc_expect(p,SC_TOK_SEMI,"';' after break/continue");return stmt;
    }

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
        return sc_parse_assignment(p,1);
    }

    sc_error_at_current(p, "expected a statement (variable declaration, assignment, or 'if')");
    /* consume one token to guarantee forward progress before synchronizing */
    if (!sc_check(p, SC_TOK_EOF) && !sc_check(p, SC_TOK_RBRACE)) sc_advance_token(p);
    return NULL;
}

static sc_stmt_list* sc_parse_stmt_block(sc_parser* p) {
    if(++p->depth>128){sc_error_at_current(p,"block nesting limit exceeded");p->current.kind=SC_TOK_EOF;--p->depth;return NULL;}
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
    --p->depth;
    return head;
}

/* ---- top-level: properties { } and material { } ---- */

static int sc_constant(sc_expr* expr,double values[16],int* count,int depth){
    if(!expr||depth>128)return 0;
    if(expr->kind==SC_EXPR_NUMBER){values[0]=expr->number_value;*count=1;return isfinite(values[0]);}
    if(expr->kind==SC_EXPR_UNARY&&expr->op[0]=='-'){
        if(!sc_constant(expr->left,values,count,depth+1))return 0;
        for(int i=0;i<*count;++i)values[i]=-values[i];
        return 1;
    }
    if(expr->kind==SC_EXPR_BINARY){
        double a[16],b[16];int na,nb;
        if(!sc_constant(expr->left,a,&na,depth+1)||!sc_constant(expr->right,b,&nb,depth+1)||na!=1||nb!=1)return 0;
        if(!strcmp(expr->op,"+"))values[0]=a[0]+b[0];else if(!strcmp(expr->op,"-"))values[0]=a[0]-b[0];else if(!strcmp(expr->op,"*"))values[0]=a[0]*b[0];else if(!strcmp(expr->op,"/")&&b[0]!=0)values[0]=a[0]/b[0];else return 0;
        *count=1;return isfinite(values[0]);
    }
    if(expr->kind==SC_EXPR_CALL){
        int n=!strcmp(expr->name,"vec2")?2:!strcmp(expr->name,"vec3")?3:!strcmp(expr->name,"vec4")?4:!strcmp(expr->name,"mat3")?9:!strcmp(expr->name,"mat4")?16:0;
        if(!n)return 0;
        int used=0;
        for(sc_expr_list* arg=expr->args;arg;arg=arg->next){double part[16];int length;
            if(!sc_constant(arg->expr,part,&length,depth+1)||used+length>n)return 0;
            for(int i=0;i<length;++i)values[used++]=part[i];
        }
        if(used==1){double value=values[0];int side=n==9?3:n==16?4:0;
            for(int i=0;i<n;++i)values[i]=side?(i/side==i%side?value:0):value;
            used=n;
        }
        if(used!=n)return 0;
        *count=n;return 1;
    }
    return 0;
}

static sc_property_decl* sc_parse_properties_block(sc_parser* p) {
    sc_expect(p, SC_TOK_LBRACE, "'{' after 'properties'");
    sc_property_decl* head = NULL;
    sc_property_decl** tail = &head;

    while (!sc_check(p, SC_TOK_RBRACE) && !sc_check(p, SC_TOK_EOF)) {
        if (!sc_check(p, SC_TOK_IDENTIFIER)) {
            sc_error_at_current(p, "expected a property name");
            sc_advance_token(p);
            sc_synchronize(p);
            continue;
        }
        sc_token name = p->current;
        sc_advance_token(p);
        sc_expect(p, SC_TOK_COLON, "':' after property name");

        if (!sc_token_is_type_keyword(p->current.kind)) {
            sc_error_at_current(p, "expected a property type (float, int, bool, vec2, vec3, vec4, mat3, mat4, texture2d)");
            sc_advance_token(p);
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
                sc_expr* expr=sc_parse_expr(p);double values[16];int count=0;
                int required=type==SC_PROP_VEC2?2:type==SC_PROP_VEC3?3:type==SC_PROP_VEC4?4:type==SC_PROP_MAT3?9:type==SC_PROP_MAT4?16:1;
                int valid=sc_constant(expr,values,&count,0)&&count==required;
                if(expr->kind==SC_EXPR_CALL&&strcmp(expr->name,sc_property_type_name(type)))valid=0;
                if(valid)for(int i=0;i<count;++i)if(!isfinite(values[i])||fabs(values[i])>FLT_MAX)valid=0;
                if(valid&&type==SC_PROP_INT&&(values[0]<INT32_MIN||values[0]>INT32_MAX||(double)(int32_t)values[0]!=values[0]))valid=0;
                if(!valid)sc_error_at_current(p,"default must be a finite constant of the declared type");
                else {decl->default_number=values[0];for(int i=0;i<count;++i)decl->default_values[i]=values[i];}
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

static void sc_parse_options(sc_parser* p,sc_shader_decl* decl){
    sc_expect(p,SC_TOK_LBRACE,"'{' after options");
    int shading=0,blending=0,sided=0;
    while(!sc_check(p,SC_TOK_RBRACE)&&!sc_check(p,SC_TOK_EOF)){
        sc_token key=p->current;sc_advance_token(p);sc_expect(p,SC_TOK_COLON,"':' after option");
        sc_token value=p->current;sc_advance_token(p);
#define SC_IS(tok,str) ((tok).length==sizeof(str)-1&&!memcmp((tok).start,str,sizeof(str)-1))
        if(SC_IS(key,"shading")){
            if(shading++)sc_error_at_current(p,"duplicate shading option");
            if(SC_IS(value,"unlit"))decl->unlit=1;else if(!SC_IS(value,"lit"))sc_error_at_current(p,"shading must be lit or unlit");
        }else if(SC_IS(key,"blending")){
            if(blending++)sc_error_at_current(p,"duplicate blending option");
            if(SC_IS(value,"transparent"))decl->transparent=1;else if(!SC_IS(value,"opaque"))sc_error_at_current(p,"blending must be opaque or transparent");
        }else if(SC_IS(key,"doubleSided")){
            if(sided++)sc_error_at_current(p,"duplicate doubleSided option");
            if(value.kind==SC_TOK_KW_TRUE)decl->double_sided=1;else if(value.kind!=SC_TOK_KW_FALSE)sc_error_at_current(p,"doubleSided must be true or false");
        }else sc_error_at_current(p,"unknown shader option");
#undef SC_IS
        sc_expect(p,SC_TOK_SEMI,"';' after option");
    }
    sc_expect(p,SC_TOK_RBRACE,"'}' after options");
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
    int saw_vertex=0,saw_options=0;

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
        }else if(sc_match(p,SC_TOK_KW_VERTEX)){
            if(saw_vertex++)sc_error_at_current(p,"duplicate vertex block");
            decl->vertex_body=sc_parse_stmt_block(p);
        }else if(sc_match(p,SC_TOK_KW_OPTIONS)){
            if(saw_options++)sc_error_at_current(p,"duplicate options block");
            sc_parse_options(p,decl);
        } else {
            sc_error_at_current(p, "expected properties, options, vertex, or material block inside shader");
            sc_advance_token(p);
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
    parser.loop_depth=0;parser.depth=0;parser.tokens=0;
    memset(&parser.previous, 0, sizeof(parser.previous));
    parser.current.kind=SC_TOK_EOF;sc_advance_token(&parser);

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
        sc_diag_add(diags, SC_DIAG_ERROR, parser.current.line, parser.current.column,
                    "unexpected trailing content; a source file must contain exactly one shader");
    }

    return program;
}
