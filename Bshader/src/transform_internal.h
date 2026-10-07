/* transform_internal.h -- concrete definitions of the opaque
 * ShaderTransformNode / ShaderTransformBuilder types from the public
 * header, plus the transform registry used by the generator. Internal. */
#ifndef BSHADER_TRANSFORM_INTERNAL_H
#define BSHADER_TRANSFORM_INTERNAL_H

#include "shader_translator.h"
#include "ast.h"
#include "arena.h"
#include "strset.h"

/* The call-expression a transform is expanding, plus enough generator
 * context (currently just the property-name set) to resolve identifiers
 * the same way top-level codegen would. */
struct ShaderTransformNode {
    sc_expr* call_expr;
    const sc_strset* properties;
    const sc_strset* samplers;
    char resolve_scratch[256]; /* backs ShaderTransformNodeResolveIdentifier */
};

/* Scratch pad a transform writes into. All string data is copied with
 * strdup-style arena/malloc ownership handled by the builder itself
 * (see transform.c) so transform authors never have to manage memory. */
struct ShaderTransformBuilder {
    char** pre_lines;
    size_t pre_line_count;
    size_t pre_line_cap;

    char** required_helpers;
    size_t required_helper_count;
    size_t required_helper_cap;

    char* replacement_expr; /* NULL until ShaderTransformBuilderSetReplacementExpr */
};

void sc_transform_builder_init(struct ShaderTransformBuilder* b);
void sc_transform_builder_dispose(struct ShaderTransformBuilder* b);

/* ---- registry ---- */

typedef struct sc_transform_entry {
    char* name;
    ShaderTransformFn fn;
    void* user_data;
} sc_transform_entry;

typedef struct sc_helper_entry {
    char* name;
    char* source;
} sc_helper_entry;

typedef struct sc_transform_registry {
    sc_transform_entry* transforms;
    size_t transform_count;
    size_t transform_cap;

    sc_helper_entry* helpers;
    size_t helper_count;
    size_t helper_cap;
} sc_transform_registry;

void sc_transform_registry_init(sc_transform_registry* reg);
void sc_transform_registry_dispose(sc_transform_registry* reg);
void sc_transform_registry_register(sc_transform_registry* reg, const char* name,
                                     ShaderTransformFn fn, void* user_data);
void sc_transform_registry_register_helper(sc_transform_registry* reg, const char* name,
                                            const char* source);
/* Returns NULL if no transform is registered under `name`. */
const sc_transform_entry* sc_transform_registry_find(const sc_transform_registry* reg, const char* name);
const sc_helper_entry* sc_transform_registry_find_helper(const sc_transform_registry* reg, const char* name);

/* Registers Bshader's built-in language features (currently: `blur`). */
void sc_register_builtin_transforms(sc_transform_registry* reg);
int sc_is_builtin_blur(const sc_transform_entry* entry);

#endif /* BSHADER_TRANSFORM_INTERNAL_H */
