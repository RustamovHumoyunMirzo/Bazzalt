#include "generator_filament.h"
#include "strbuf.h"
#include "strset.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- output-field mapping (color = ..., roughness = ..., etc) ---- */

typedef struct sc_output_field {
    const char* bshader_name;
    const char* filament_lvalue;
} sc_output_field;

static const sc_output_field SC_OUTPUT_FIELDS[] = {
    {"color", "material.baseColor"},
    {"alpha", "material.baseColor.a"},
    {"roughness", "material.roughness"},
    {"metallic", "material.metallic"},
    {"normal", "material.normal"},
    {"ao", "material.ambientOcclusion"},
    {"emissive", "material.emissive"},
};
#define SC_OUTPUT_FIELD_COUNT (sizeof(SC_OUTPUT_FIELDS) / sizeof(SC_OUTPUT_FIELDS[0]))

static const char* sc_lookup_output_field(const char* name) {
    for (size_t i = 0; i < SC_OUTPUT_FIELD_COUNT; i++) {
        if (strcmp(SC_OUTPUT_FIELDS[i].bshader_name, name) == 0) return SC_OUTPUT_FIELDS[i].filament_lvalue;
    }
    return NULL;
}

/* ---- built-in identifiers available inside every material { } block,
 * independent of user-declared properties or locals (mirrors the
 * fragment-stage inputs a real material shader would have in scope) ---- */

static const char* SC_BUILTIN_IDENTIFIERS[] = {
    "UV", "worldPosition", "worldNormal", "vertexColor", "time",
};
#define SC_BUILTIN_IDENTIFIER_COUNT (sizeof(SC_BUILTIN_IDENTIFIERS) / sizeof(SC_BUILTIN_IDENTIFIERS[0]))

static int sc_is_builtin_identifier(const char* name) {
    for (size_t i = 0; i < SC_BUILTIN_IDENTIFIER_COUNT; i++) {
        if (strcmp(SC_BUILTIN_IDENTIFIERS[i], name) == 0) return 1;
    }
    return 0;
}

/* ---- property type -> Filament parameter type text ---- */

static const char* sc_filament_param_type(sc_property_type type) {
    switch (type) {
        case SC_PROP_FLOAT: return "float";
        case SC_PROP_INT: return "int";
        case SC_PROP_BOOL: return "bool";
        case SC_PROP_VEC2: return "float2";
        case SC_PROP_VEC3: return "float3";
        case SC_PROP_VEC4: return "float4";
        case SC_PROP_TEXTURE2D: return "sampler2d";
        default: return "float";
    }
}

/* Bshader property references are lowered to a flat `materialParams_<name>`
 * identifier in generated code, regardless of type -- scalars and
 * samplers alike. Kept as one convention so expression codegen doesn't
 * need to special-case member access on parameters. */
static void sc_emit_property_ref(sc_strbuf* out, const char* name) {
    sc_strbuf_appendf(out, "materialParams_%s", name);
}

/* ---- expression codegen ---- */

typedef struct sc_gen_ctx {
    const sc_transform_registry* registry;
    sc_strset* locals;
    sc_strset* properties;
    sc_strset* required_helpers; /* global, dedups helper injection */
    sc_diag_list* diags;
    int had_semantic_error;
} sc_gen_ctx;

static void sc_emit_expr(sc_gen_ctx* ctx, sc_expr* expr, sc_strbuf* pre_out, sc_strbuf* out);

static void sc_emit_call(sc_gen_ctx* ctx, sc_expr* expr, sc_strbuf* pre_out, sc_strbuf* out) {
    const sc_transform_entry* transform = sc_transform_registry_find(ctx->registry, expr->name);

    if (transform) {
        ShaderTransformNode node;
        node.call_expr = expr;
        node.properties = ctx->properties;
        node.resolve_scratch[0] = '\0';
        struct ShaderTransformBuilder builder;
        sc_transform_builder_init(&builder);

        transform->fn(&node, &builder, transform->user_data);

        for (size_t i = 0; i < builder.pre_line_count; i++) {
            sc_strbuf_appendf(pre_out, "        %s\n", builder.pre_lines[i]);
        }
        for (size_t i = 0; i < builder.required_helper_count; i++) {
            /* The builder (and the strings it owns) is disposed below,
             * before this shared, longer-lived set is consumed -- so
             * this set must hold its own copies, not raw pointers into
             * builder-owned memory. Freed alongside the set itself at
             * the end of sc_generate_filament_material(). */
            if (!sc_strset_contains(ctx->required_helpers, builder.required_helpers[i])) {
                sc_strset_add(ctx->required_helpers, sc_strdup(builder.required_helpers[i]));
            }
        }

        if (builder.replacement_expr) {
            sc_strbuf_append(out, builder.replacement_expr);
        } else {
            sc_diag_add(ctx->diags, SC_DIAG_WARNING, expr->line, 1,
                        "transform '%s' did not produce a replacement expression; using 0.0",
                        expr->name);
            sc_strbuf_append(out, "0.0");
        }

        sc_transform_builder_dispose(&builder);
        return;
    }

    /* Not a registered transform: pass through as an ordinary function
     * call. This covers built-in intrinsics like texture(sampler, uv)
     * as well as any target-language function the author calls
     * directly. */
    sc_strbuf_appendf(out, "%s(", expr->name);
    for (sc_expr_list* it = expr->args; it; it = it->next) {
        sc_emit_expr(ctx, it->expr, pre_out, out);
        if (it->next) sc_strbuf_append(out, ", ");
    }
    sc_strbuf_append(out, ")");
}

static void sc_emit_expr(sc_gen_ctx* ctx, sc_expr* expr, sc_strbuf* pre_out, sc_strbuf* out) {
    if (!expr) {
        sc_strbuf_append(out, "0.0");
        return;
    }

    switch (expr->kind) {
        case SC_EXPR_NUMBER: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", expr->number_value);
            sc_strbuf_append(out, buf);
            break;
        }
        case SC_EXPR_BOOL:
            sc_strbuf_append(out, expr->bool_value ? "true" : "false");
            break;
        case SC_EXPR_IDENTIFIER:
            if (sc_strset_contains(ctx->properties, expr->name)) {
                sc_emit_property_ref(out, expr->name);
            } else if (sc_strset_contains(ctx->locals, expr->name)) {
                sc_strbuf_append(out, expr->name);
            } else if (sc_is_builtin_identifier(expr->name)) {
                sc_strbuf_append(out, expr->name);
            } else {
                sc_diag_add(ctx->diags, SC_DIAG_ERROR, expr->line, 1,
                            "use of undeclared identifier '%s'", expr->name);
                ctx->had_semantic_error = 1;
                sc_strbuf_append(out, expr->name);
            }
            break;
        case SC_EXPR_CALL:
            sc_emit_call(ctx, expr, pre_out, out);
            break;
        case SC_EXPR_BINARY:
            sc_strbuf_append(out, "(");
            sc_emit_expr(ctx, expr->left, pre_out, out);
            sc_strbuf_appendf(out, " %s ", expr->op);
            sc_emit_expr(ctx, expr->right, pre_out, out);
            sc_strbuf_append(out, ")");
            break;
        case SC_EXPR_UNARY:
            sc_strbuf_appendf(out, "%s", expr->op);
            sc_emit_expr(ctx, expr->left, pre_out, out);
            break;
        case SC_EXPR_MEMBER:
            sc_emit_expr(ctx, expr->object, pre_out, out);
            sc_strbuf_appendf(out, ".%s", expr->member);
            break;
    }
}

/* ---- statement codegen ---- */

static void sc_emit_stmts(sc_gen_ctx* ctx, sc_stmt_list* stmts, sc_strbuf* out, int indent);

static void sc_indent(sc_strbuf* out, int indent) {
    for (int i = 0; i < indent; i++) sc_strbuf_append(out, "    ");
}

static void sc_emit_stmt(sc_gen_ctx* ctx, sc_stmt* stmt, sc_strbuf* out, int indent) {
    switch (stmt->kind) {
        case SC_STMT_VAR_DECL: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf value;
            sc_strbuf_init(&value);
            if (stmt->decl_init) {
                sc_emit_expr(ctx, stmt->decl_init, &pre, &value);
            }
            if (pre.length > 0) sc_strbuf_append(out, pre.data);

            sc_strset_add(ctx->locals, stmt->decl_name);
            sc_indent(out, indent);
            sc_strbuf_appendf(out, "%s %s", sc_filament_param_type(stmt->decl_type), stmt->decl_name);
            if (stmt->decl_init) {
                sc_strbuf_appendf(out, " = %s", value.data ? value.data : "0.0");
            }
            sc_strbuf_append(out, ";\n");

            sc_strbuf_free(&pre);
            sc_strbuf_free(&value);
            break;
        }
        case SC_STMT_ASSIGN: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf value;
            sc_strbuf_init(&value);
            sc_emit_expr(ctx, stmt->assign_value, &pre, &value);
            if (pre.length > 0) sc_strbuf_append(out, pre.data);

            const char* output_field = sc_lookup_output_field(stmt->assign_target);
            sc_indent(out, indent);
            if (output_field) {
                sc_strbuf_appendf(out, "%s = %s;\n", output_field, value.data ? value.data : "0.0");
            } else if (sc_strset_contains(ctx->locals, stmt->assign_target)) {
                sc_strbuf_appendf(out, "%s = %s;\n", stmt->assign_target, value.data ? value.data : "0.0");
            } else if (sc_strset_contains(ctx->properties, stmt->assign_target)) {
                sc_diag_add(ctx->diags, SC_DIAG_ERROR, stmt->line, 1,
                            "cannot assign to property '%s'; properties are read-only inputs",
                            stmt->assign_target);
                ctx->had_semantic_error = 1;
                sc_strbuf_appendf(out, "/* skipped invalid assignment to property '%s' */\n", stmt->assign_target);
            } else {
                sc_diag_add(ctx->diags, SC_DIAG_ERROR, stmt->line, 1,
                            "assignment to undeclared variable '%s' (expected a local variable or one of: "
                            "color, alpha, roughness, metallic, normal, ao, emissive)",
                            stmt->assign_target);
                ctx->had_semantic_error = 1;
                sc_strbuf_appendf(out, "/* skipped invalid assignment to '%s' */\n", stmt->assign_target);
            }

            sc_strbuf_free(&pre);
            sc_strbuf_free(&value);
            break;
        }
        case SC_STMT_IF: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf cond;
            sc_strbuf_init(&cond);
            sc_emit_expr(ctx, stmt->if_cond, &pre, &cond);
            if (pre.length > 0) sc_strbuf_append(out, pre.data);

            sc_indent(out, indent);
            sc_strbuf_appendf(out, "if (%s) {\n", cond.data ? cond.data : "false");
            sc_emit_stmts(ctx, stmt->if_then, out, indent + 1);
            sc_indent(out, indent);
            sc_strbuf_append(out, "}\n");
            if (stmt->if_else) {
                sc_indent(out, indent);
                sc_strbuf_append(out, "else {\n");
                sc_emit_stmts(ctx, stmt->if_else, out, indent + 1);
                sc_indent(out, indent);
                sc_strbuf_append(out, "}\n");
            }

            sc_strbuf_free(&pre);
            sc_strbuf_free(&cond);
            break;
        }
        case SC_STMT_EXPR: {
            sc_strbuf pre;
            sc_strbuf_init(&pre);
            sc_strbuf value;
            sc_strbuf_init(&value);
            sc_emit_expr(ctx, stmt->expr, &pre, &value);
            if (pre.length > 0) sc_strbuf_append(out, pre.data);
            sc_indent(out, indent);
            sc_strbuf_appendf(out, "%s;\n", value.data ? value.data : "");
            sc_strbuf_free(&pre);
            sc_strbuf_free(&value);
            break;
        }
    }
}

static void sc_emit_stmts(sc_gen_ctx* ctx, sc_stmt_list* stmts, sc_strbuf* out, int indent) {
    for (sc_stmt_list* it = stmts; it; it = it->next) {
        sc_emit_stmt(ctx, it->stmt, out, indent);
    }
}

/* ---- top level ---- */

static void sc_emit_parameter_default(sc_strbuf* out, const sc_property_decl* prop) {
    if (!prop->has_default) return;
    switch (prop->type) {
        case SC_PROP_BOOL:
            sc_strbuf_appendf(out, ", default : %s", prop->default_bool ? "true" : "false");
            break;
        case SC_PROP_FLOAT:
        case SC_PROP_INT:
            sc_strbuf_appendf(out, ", default : %g", prop->default_number);
            break;
        default:
            break; /* vec/texture defaults are not supported (parser already errors on texture) */
    }
}

char* sc_generate_filament_material(const sc_program* program,
                                     const sc_transform_registry* registry,
                                     sc_diag_list* diags,
                                     size_t* out_length) {
    if (!program || !program->shader) {
        if (out_length) *out_length = 0;
        return NULL;
    }

    const sc_shader_decl* shader = program->shader;

    sc_strset properties;
    sc_strset_init(&properties);
    for (sc_property_decl* p = shader->properties; p; p = p->next) {
        sc_strset_add(&properties, p->name);
    }

    sc_strset locals;
    sc_strset_init(&locals);

    sc_strset required_helpers;
    sc_strset_init(&required_helpers);

    sc_gen_ctx ctx;
    ctx.registry = registry;
    ctx.locals = &locals;
    ctx.properties = &properties;
    ctx.required_helpers = &required_helpers;
    ctx.diags = diags;
    ctx.had_semantic_error = 0;

    /* -- material { } fragment body -- */
    sc_strbuf body;
    sc_strbuf_init(&body);
    sc_emit_stmts(&ctx, shader->material_body, &body, 2);

    /* -- assemble final output -- */
    sc_strbuf out;
    sc_strbuf_init(&out);

    sc_strbuf_appendf(&out, "// Generated by Bshader -- do not edit by hand.\n");
    sc_strbuf_appendf(&out, "material {\n");
    sc_strbuf_appendf(&out, "    name : \"%s\",\n", shader->name);
    sc_strbuf_appendf(&out, "    parameters : [\n");
    size_t param_index = 0;
    size_t param_count = 0;
    for (sc_property_decl* p = shader->properties; p; p = p->next) param_count++;
    for (sc_property_decl* p = shader->properties; p; p = p->next, param_index++) {
        sc_strbuf_appendf(&out, "        { type : %s, name : %s",
                           sc_filament_param_type(p->type), p->name);
        sc_emit_parameter_default(&out, p);
        sc_strbuf_appendf(&out, " }%s\n", (param_index + 1 < param_count) ? "," : "");
    }
    sc_strbuf_appendf(&out, "    ],\n");
    sc_strbuf_appendf(&out, "    shadingModel : lit,\n");
    sc_strbuf_appendf(&out, "    blending : opaque\n");
    sc_strbuf_appendf(&out, "}\n\n");

    sc_strbuf_appendf(&out, "fragment {\n");
    for (size_t i = 0; i < required_helpers.count; i++) {
        const sc_helper_entry* helper = sc_transform_registry_find_helper(registry, required_helpers.items[i]);
        if (helper) {
            sc_strbuf_append(&out, helper->source);
            sc_strbuf_append(&out, "\n");
        }
    }
    sc_strbuf_appendf(&out, "    void material(inout MaterialInputs material) {\n");
    sc_strbuf_appendf(&out, "        prepareMaterial(material);\n");
    sc_strbuf_append(&out, body.data ? body.data : "");
    sc_strbuf_appendf(&out, "    }\n");
    sc_strbuf_appendf(&out, "}\n");

    sc_strbuf_free(&body);
    sc_strset_free(&properties);
    sc_strset_free(&locals);
    for (size_t i = 0; i < required_helpers.count; i++) free(required_helpers.items[i]);
    sc_strset_free(&required_helpers);

    if (out_length) *out_length = out.length;
    return sc_strbuf_take(&out);
}
