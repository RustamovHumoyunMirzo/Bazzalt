#include "transform_internal.h"
#include "strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===================== registry ===================== */

void sc_transform_registry_init(sc_transform_registry* reg) {
    memset(reg, 0, sizeof(*reg));
}

void sc_transform_registry_dispose(sc_transform_registry* reg) {
    for (size_t i = 0; i < reg->transform_count; i++) free(reg->transforms[i].name);
    free(reg->transforms);
    for (size_t i = 0; i < reg->helper_count; i++) {
        free(reg->helpers[i].name);
        free(reg->helpers[i].source);
    }
    free(reg->helpers);
    memset(reg, 0, sizeof(*reg));
}

void sc_transform_registry_register(sc_transform_registry* reg, const char* name,
                                     ShaderTransformFn fn, void* user_data) {
    for (size_t i = 0; i < reg->transform_count; i++) {
        if (strcmp(reg->transforms[i].name, name) == 0) {
            reg->transforms[i].fn = fn;
            reg->transforms[i].user_data = user_data;
            return;
        }
    }
    if (reg->transform_count == reg->transform_cap) {
        size_t new_cap = reg->transform_cap == 0 ? 4 : reg->transform_cap * 2;
        reg->transforms = (sc_transform_entry*)realloc(reg->transforms, new_cap * sizeof(sc_transform_entry));
        reg->transform_cap = new_cap;
    }
    sc_transform_entry* e = &reg->transforms[reg->transform_count++];
    e->name = sc_strdup(name);
    e->fn = fn;
    e->user_data = user_data;
}

void sc_transform_registry_register_helper(sc_transform_registry* reg, const char* name, const char* source) {
    for (size_t i = 0; i < reg->helper_count; i++) {
        if (strcmp(reg->helpers[i].name, name) == 0) {
            free(reg->helpers[i].source);
            reg->helpers[i].source = sc_strdup(source);
            return;
        }
    }
    if (reg->helper_count == reg->helper_cap) {
        size_t new_cap = reg->helper_cap == 0 ? 4 : reg->helper_cap * 2;
        reg->helpers = (sc_helper_entry*)realloc(reg->helpers, new_cap * sizeof(sc_helper_entry));
        reg->helper_cap = new_cap;
    }
    sc_helper_entry* e = &reg->helpers[reg->helper_count++];
    e->name = sc_strdup(name);
    e->source = sc_strdup(source);
}

const sc_transform_entry* sc_transform_registry_find(const sc_transform_registry* reg, const char* name) {
    for (size_t i = 0; i < reg->transform_count; i++) {
        if (strcmp(reg->transforms[i].name, name) == 0) return &reg->transforms[i];
    }
    return NULL;
}

const sc_helper_entry* sc_transform_registry_find_helper(const sc_transform_registry* reg, const char* name) {
    for (size_t i = 0; i < reg->helper_count; i++) {
        if (strcmp(reg->helpers[i].name, name) == 0) return &reg->helpers[i];
    }
    return NULL;
}

/* ===================== builder ===================== */

void sc_transform_builder_init(struct ShaderTransformBuilder* b) {
    memset(b, 0, sizeof(*b));
}

void sc_transform_builder_dispose(struct ShaderTransformBuilder* b) {
    for (size_t i = 0; i < b->pre_line_count; i++) free(b->pre_lines[i]);
    free(b->pre_lines);
    for (size_t i = 0; i < b->required_helper_count; i++) free(b->required_helpers[i]);
    free(b->required_helpers);
    free(b->replacement_expr);
    memset(b, 0, sizeof(*b));
}

void ShaderTransformBuilderEmitLine(ShaderTransformBuilder* builder, const char* code) {
    if (!builder || !code) return;
    if (builder->pre_line_count == builder->pre_line_cap) {
        size_t new_cap = builder->pre_line_cap == 0 ? 4 : builder->pre_line_cap * 2;
        builder->pre_lines = (char**)realloc(builder->pre_lines, new_cap * sizeof(char*));
        builder->pre_line_cap = new_cap;
    }
    builder->pre_lines[builder->pre_line_count++] = sc_strdup(code);
}

void ShaderTransformBuilderRequireHelper(ShaderTransformBuilder* builder, const char* helperName) {
    if (!builder || !helperName) return;
    for (size_t i = 0; i < builder->required_helper_count; i++) {
        if (strcmp(builder->required_helpers[i], helperName) == 0) return;
    }
    if (builder->required_helper_count == builder->required_helper_cap) {
        size_t new_cap = builder->required_helper_cap == 0 ? 4 : builder->required_helper_cap * 2;
        builder->required_helpers = (char**)realloc(builder->required_helpers, new_cap * sizeof(char*));
        builder->required_helper_cap = new_cap;
    }
    builder->required_helpers[builder->required_helper_count++] = sc_strdup(helperName);
}

void ShaderTransformBuilderSetReplacementExpr(ShaderTransformBuilder* builder, const char* exprText) {
    if (!builder || !exprText) return;
    free(builder->replacement_expr);
    builder->replacement_expr = sc_strdup(exprText);
}

/* ===================== node accessors ===================== */

size_t ShaderTransformNodeGetArgCount(const ShaderTransformNode* node) {
    if (!node || !node->call_expr) return 0;
    size_t count = 0;
    for (sc_expr_list* it = node->call_expr->args; it; it = it->next) count++;
    return count;
}

static sc_expr* sc_transform_node_arg(const ShaderTransformNode* node, size_t index) {
    if (!node || !node->call_expr) return NULL;
    size_t i = 0;
    for (sc_expr_list* it = node->call_expr->args; it; it = it->next, i++) {
        if (i == index) return it->expr;
    }
    return NULL;
}

const char* ShaderTransformNodeGetArgAsIdentifier(const ShaderTransformNode* node, size_t index) {
    sc_expr* e = sc_transform_node_arg(node, index);
    if (e && e->kind == SC_EXPR_IDENTIFIER) return e->name;
    return NULL;
}

double ShaderTransformNodeGetArgAsNumber(const ShaderTransformNode* node, size_t index) {
    sc_expr* e = sc_transform_node_arg(node, index);
    if (!e) return 0.0;
    if (e->kind == SC_EXPR_NUMBER) return e->number_value;
    if (e->kind == SC_EXPR_UNARY && e->op[0] == '-' && e->left && e->left->kind == SC_EXPR_NUMBER) {
        return -e->left->number_value;
    }
    return 0.0;
}

bool ShaderTransformNodeArgIsProperty(const ShaderTransformNode* node, size_t index) {
    sc_expr* e = sc_transform_node_arg(node, index);
    if (!e || e->kind != SC_EXPR_IDENTIFIER || !node->properties) return false;
    return sc_strset_contains(node->properties, e->name) ? true : false;
}

const char* ShaderTransformNodeResolveIdentifier(const ShaderTransformNode* node, const char* identifier) {
    if (!node || !identifier) return identifier;
    if (node->properties && sc_strset_contains(node->properties, identifier)) {
        snprintf(((ShaderTransformNode*)node)->resolve_scratch,
                 sizeof(node->resolve_scratch), node->samplers&&sc_strset_contains(node->samplers,identifier)?"materialParams_%s":"materialParams.%s", identifier);
        return node->resolve_scratch;
    }
    return identifier;
}

/* ===================== built-in transforms ===================== */

/*
 * `blur(source, uv, radius)` is Bshader's built-in high-level blur
 * feature. It expands to a call to a generated multi-tap sampling
 * helper rather than a single texture lookup, so authors get real
 * blur behavior from one line of Bshader instead of hand-writing the
 * tap loop themselves. This is also the reference example that
 * DOCS.md walks through for anyone adding their own transform.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

static void sc_builtin_blur_transform(ShaderTransformNode* node,
                                       ShaderTransformBuilder* builder,
                                       void* userData) {
    (void)userData;

    size_t argc = ShaderTransformNodeGetArgCount(node);
    if (argc < 2) {
        ShaderTransformBuilderSetReplacementExpr(builder, "vec4(0.0)");
        return;
    }

    const char* source_raw = ShaderTransformNodeGetArgAsIdentifier(node, 0);
    const char* uv_raw = ShaderTransformNodeGetArgAsIdentifier(node, 1);
    const char* source = source_raw ? ShaderTransformNodeResolveIdentifier(node, source_raw) : "materialParams_baseColor";
    /* ShaderTransformNodeResolveIdentifier reuses one scratch buffer per
     * node, so resolve+copy `source` before resolving `uv` (which would
     * otherwise overwrite it). */
    char source_buf[256];
    snprintf(source_buf, sizeof(source_buf), "%s", source);
    const char* uv = uv_raw ? ShaderTransformNodeResolveIdentifier(node, uv_raw) : "UV";

    /* The radius argument may be a literal (blur(tex, UV, 2.0)) or an
     * identifier such as a property (blur(tex, UV, blurRadius)). */
    char radius_buf[300];
    sc_expr* radius_arg = sc_transform_node_arg(node, 2);
    if (argc >= 3 && radius_arg && radius_arg->kind == SC_EXPR_IDENTIFIER) {
        snprintf(radius_buf, sizeof(radius_buf), "%s", ShaderTransformNodeResolveIdentifier(node, radius_arg->name));
    } else {
        double radius = (argc >= 3) ? ShaderTransformNodeGetArgAsNumber(node, 2) : 1.0;
        snprintf(radius_buf, sizeof(radius_buf), "%g", radius);
    }

    ShaderTransformBuilderRequireHelper(builder, "sc_blur_sample");

    char expr[600];
    snprintf(expr, sizeof(expr), "sc_blur_sample(%s, %s, %s)", source_buf, uv, radius_buf);
    ShaderTransformBuilderSetReplacementExpr(builder, expr);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static const char* SC_BLUR_HELPER_SOURCE =
    "vec4 sc_blur_sample(sampler2D tex, vec2 uv, float radius) {\n"
    "    vec2 texel = radius / vec2(textureSize(tex, 0));\n"
    "    vec4 sum = vec4(0.0);\n"
    "    sum += texture(tex, uv + texel * vec2(-1.0, -1.0));\n"
    "    sum += texture(tex, uv + texel * vec2( 1.0, -1.0));\n"
    "    sum += texture(tex, uv + texel * vec2(-1.0,  1.0));\n"
    "    sum += texture(tex, uv + texel * vec2( 1.0,  1.0));\n"
    "    sum += texture(tex, uv) * 2.0;\n"
    "    return sum / 6.0;\n"
    "}\n";

int sc_is_builtin_blur(const sc_transform_entry* entry){return entry&&entry->fn==sc_builtin_blur_transform;}

void sc_register_builtin_transforms(sc_transform_registry* reg) {
    sc_transform_registry_register(reg, "blur", sc_builtin_blur_transform, NULL);
    sc_transform_registry_register_helper(reg, "sc_blur_sample", SC_BLUR_HELPER_SOURCE);
}
