#include "shader_translator.h"
#include "arena.h"
#include "diag.h"
#include "ast.h"
#include "parser.h"
#include "transform_internal.h"
#include "generator_filament.h"
#include "strbuf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------- */
/* ShaderContext                                                        */
/* -------------------------------------------------------------------- */

struct ShaderContext {
    sc_arena arena;              /* backs parsing/AST/diagnostics for the
                                     current (or most recent) compile */
    sc_transform_registry registry; /* malloc-backed; survives resets */
};

ShaderContext* ShaderContextCreate(void) {
    ShaderContext* ctx = (ShaderContext*)malloc(sizeof(ShaderContext));
    if (!ctx) return NULL;

    sc_arena_init(&ctx->arena, 0);
    sc_transform_registry_init(&ctx->registry);
    sc_register_builtin_transforms(&ctx->registry);

    return ctx;
}

void ShaderContextDestroy(ShaderContext* ctx) {
    if (!ctx) return;
    sc_transform_registry_dispose(&ctx->registry);
    sc_arena_free(&ctx->arena);
    free(ctx);
}

void ShaderContextReset(ShaderContext* ctx) {
    if (!ctx) return;
    sc_arena_reset(&ctx->arena);
}

void ShaderContextRegisterTransform(ShaderContext* ctx, const char* functionName,
                                     ShaderTransformFn fn, void* userData) {
    if (!ctx || !functionName || !fn) return;
    sc_transform_registry_register(&ctx->registry, functionName, fn, userData);
}

void ShaderContextRegisterHelperSource(ShaderContext* ctx, const char* helperName,
                                        const char* sourceCode) {
    if (!ctx || !helperName || !sourceCode) return;
    sc_transform_registry_register_helper(&ctx->registry, helperName, sourceCode);
}

/* -------------------------------------------------------------------- */
/* ShaderCompilationResult                                              */
/* -------------------------------------------------------------------- */

struct ShaderCompilationResult {
    int success;

    char* output;       /* malloc'd; NULL on failure */
    size_t output_size;

    char* error;         /* malloc'd summary message; NULL on success */

    ShaderDiagnostic* diagnostics; /* malloc'd array; .message points into diag_messages */
    char** diag_messages;          /* malloc'd array of malloc'd strings, owns the text */
    size_t diagnostic_count;
};

/* Copy the arena-backed diagnostics collected during this compile into
 * independently malloc'd storage owned by the result, since the arena
 * they live in may be reset or freed by the caller at any time after
 * ShaderTranslatorCompile() returns. */
static void sc_result_copy_diagnostics(ShaderCompilationResult* result, const sc_diag_list* diags) {
    result->diagnostic_count = diags->count;
    if (diags->count == 0) {
        result->diagnostics = NULL;
        result->diag_messages = NULL;
        return;
    }

    result->diagnostics = (ShaderDiagnostic*)malloc(diags->count * sizeof(ShaderDiagnostic));
    result->diag_messages = (char**)malloc(diags->count * sizeof(char*));

    for (size_t i = 0; i < diags->count; i++) {
        const sc_diag_entry* src = &diags->entries[i];
        result->diag_messages[i] = sc_strdup(src->message);
        result->diagnostics[i].severity = (src->severity == SC_DIAG_ERROR)
                                               ? ShaderDiagnosticSeverity_Error
                                               : ShaderDiagnosticSeverity_Warning;
        result->diagnostics[i].line = src->line;
        result->diagnostics[i].column = src->column;
        result->diagnostics[i].message = result->diag_messages[i];
    }
}

static char* sc_build_error_summary(const sc_diag_list* diags) {
    /* Prefer surfacing the first actual error verbatim; it's almost
     * always more useful than a generic "N errors occurred" line, and
     * callers who want the full list can walk the diagnostics array. */
    for (size_t i = 0; i < diags->count; i++) {
        if (diags->entries[i].severity == SC_DIAG_ERROR) {
            char buf[600];
            snprintf(buf, sizeof(buf), "line %d: %s", diags->entries[i].line, diags->entries[i].message);
            return sc_strdup(buf);
        }
    }
    return sc_strdup("compilation failed");
}

ShaderCompilationResult* ShaderTranslatorCompile(ShaderContext* ctx,
                                                  const char* sourceCode,
                                                  size_t sourceSize) {
    ShaderCompilationResult* result = (ShaderCompilationResult*)malloc(sizeof(ShaderCompilationResult));
    if(!result)return NULL;
    memset(result, 0, sizeof(*result));

    if (!ctx || !sourceCode || sourceSize>4u*1024u*1024u) {
        result->success = 0;
        result->error = sc_strdup("invalid arguments: context/source must be valid and source size must not exceed 4 MiB");
        return result;
    }

    sc_diag_list diags;
    sc_diag_list_init(&diags, &ctx->arena);

    sc_program* program = sc_parse_program(&ctx->arena, &diags, sourceCode, sourceSize);

    char* output = NULL;
    size_t output_size = 0;

    if (!sc_diag_has_errors(&diags) && program && program->shader) {
        output = sc_generate_filament_material(program, &ctx->registry, &diags, &output_size);
    }

    if (sc_diag_has_errors(&diags)) {
        free(output);
        output = NULL;
        output_size = 0;
        result->success = 0;
        result->error = sc_build_error_summary(&diags);
    } else {
        result->success = 1;
        result->output = output;
        result->output_size = output_size;
        result->error = NULL;
    }

    sc_result_copy_diagnostics(result, &diags);

    return result;
}

ShaderCompilationResult* ShaderTranslatorCompileString(ShaderContext* ctx, const char* sourceCode) {
    return ShaderTranslatorCompile(ctx, sourceCode, sourceCode ? strlen(sourceCode) : 0);
}

void ShaderCompilationResultFree(ShaderCompilationResult* result) {
    if (!result) return;
    free(result->output);
    free(result->error);
    if (result->diag_messages) {
        for (size_t i = 0; i < result->diagnostic_count; i++) {
            free(result->diag_messages[i]);
        }
        free(result->diag_messages);
    }
    free(result->diagnostics);
    free(result);
}

/* -------------------------------------------------------------------- */
/* Result inspection                                                    */
/* -------------------------------------------------------------------- */

bool ShaderResultIsSuccess(const ShaderCompilationResult* result) {
    return result && result->success;
}

const char* ShaderResultGetOutput(const ShaderCompilationResult* result) {
    return (result && result->success) ? result->output : NULL;
}

size_t ShaderResultGetOutputSize(const ShaderCompilationResult* result) {
    return (result && result->success) ? result->output_size : 0;
}

const char* ShaderResultGetError(const ShaderCompilationResult* result) {
    return result ? result->error : NULL;
}

size_t ShaderResultGetDiagnosticCount(const ShaderCompilationResult* result) {
    return result ? result->diagnostic_count : 0;
}

ShaderDiagnostic ShaderResultGetDiagnostic(const ShaderCompilationResult* result, size_t index) {
    ShaderDiagnostic empty;
    memset(&empty, 0, sizeof(empty));
    if (!result || index >= result->diagnostic_count) return empty;
    return result->diagnostics[index];
}
