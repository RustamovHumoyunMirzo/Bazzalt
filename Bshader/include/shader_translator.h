/**
 * shader_translator.h
 * -----------------------------------------------------------------------
 * Public API for the Bshader compiler library.
 *
 * Bshader is a small, high-level shading language and compiler. Source
 * code written in Bshader is parsed into an AST, run through an
 * extensible transform pipeline (for high-level features such as
 * `blur(...)`), and lowered into a fully self-contained, ready-to-use
 * material payload.
 *
 * This header is the ONLY contract client code should depend on. Nothing
 * about the internal representation used for the compiled output is
 * exposed here, and none of it should be assumed by callers -- treat the
 * string returned by ShaderResultGetOutput() as an opaque compiled
 * artifact to be written to disk or handed to your renderer's material
 * loader.
 *
 * Naming convention: all public identifiers use PascalCase.
 * -----------------------------------------------------------------------
 */

#ifndef BSHADER_SHADER_TRANSLATOR_H
#define BSHADER_SHADER_TRANSLATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/* -------------------------------------------------------------------- */
/* Version                                                              */
/* -------------------------------------------------------------------- */

#define BSHADER_VERSION_MAJOR 1
#define BSHADER_VERSION_MINOR 0
#define BSHADER_VERSION_PATCH 0

/* -------------------------------------------------------------------- */
/* Opaque types                                                         */
/* -------------------------------------------------------------------- */

/** Compiler context. Owns an internal arena allocator and configuration.
 *  Not thread-safe: use one context per thread, or serialize access. */
typedef struct ShaderContext ShaderContext;

/** Result of a single compilation. Owns its own error/output strings. */
typedef struct ShaderCompilationResult ShaderCompilationResult;

/* -------------------------------------------------------------------- */
/* Lifecycle                                                            */
/* -------------------------------------------------------------------- */

/**
 * Create a new compiler context. Allocates an internal memory arena used
 * for all parsing/transform work performed through this context.
 *
 * Returns NULL on allocation failure.
 */
ShaderContext* ShaderContextCreate(void);

/**
 * Destroy a compiler context and free all memory owned by it, including
 * the arena used during compilation. Any ShaderCompilationResult objects
 * previously produced by this context remain valid until they are freed
 * individually with ShaderCompilationResultFree() -- they do not depend
 * on the context's arena.
 */
void ShaderContextDestroy(ShaderContext* ctx);

/**
 * Reset a context's internal arena, releasing memory used by previous
 * compilations while keeping the context (and any registered custom
 * transforms, see ShaderContextRegisterTransform) usable for further
 * compiles. Cheaper than destroying and recreating a context in a loop.
 */
void ShaderContextReset(ShaderContext* ctx);

/* -------------------------------------------------------------------- */
/* Compilation                                                          */
/* -------------------------------------------------------------------- */

/**
 * Compile Bshader source code into a ready-to-use compiled material.
 *
 * ctx        - a valid context created with ShaderContextCreate().
 * sourceCode - a buffer containing Bshader source text. Does not need to
 *              be NUL-terminated if sourceSize is provided accurately.
 * sourceSize - length of sourceCode in bytes.
 *
 * Always returns a non-NULL ShaderCompilationResult (even on failure);
 * check ShaderResultIsSuccess() to determine outcome. The caller owns
 * the returned result and must release it with
 * ShaderCompilationResultFree().
 */
ShaderCompilationResult* ShaderTranslatorCompile(ShaderContext* ctx,
                                                  const char* sourceCode,
                                                  size_t sourceSize);

/**
 * Convenience wrapper for compiling a NUL-terminated C string.
 */
ShaderCompilationResult* ShaderTranslatorCompileString(ShaderContext* ctx,
                                                        const char* sourceCode);

/**
 * Free a compilation result and everything it owns (output buffer,
 * diagnostics list, error message).
 */
void ShaderCompilationResultFree(ShaderCompilationResult* result);

/* -------------------------------------------------------------------- */
/* Result inspection                                                    */
/* -------------------------------------------------------------------- */

/** Returns true if compilation succeeded and output is available. */
bool ShaderResultIsSuccess(const ShaderCompilationResult* result);

/**
 * Returns the compiled material payload as a NUL-terminated string, or
 * NULL if compilation failed. The returned pointer is owned by the
 * result and stays valid until ShaderCompilationResultFree() is called.
 * Treat the contents as an opaque artifact -- write it to a file with
 * whatever extension your runtime expects, or hand it directly to your
 * material-loading code.
 */
const char* ShaderResultGetOutput(const ShaderCompilationResult* result);

/** Length in bytes of the buffer returned by ShaderResultGetOutput(). */
size_t ShaderResultGetOutputSize(const ShaderCompilationResult* result);

/**
 * Returns a human-readable error message describing why compilation
 * failed, or NULL if it succeeded. Owned by the result.
 */
const char* ShaderResultGetError(const ShaderCompilationResult* result);

/** Number of diagnostic entries (errors/warnings) attached to the result. */
size_t ShaderResultGetDiagnosticCount(const ShaderCompilationResult* result);

/** Severity levels for a diagnostic entry. */
typedef enum ShaderDiagnosticSeverity {
    ShaderDiagnosticSeverity_Warning = 0,
    ShaderDiagnosticSeverity_Error = 1
} ShaderDiagnosticSeverity;

/** A single diagnostic (error or warning) with source location info. */
typedef struct ShaderDiagnostic {
    ShaderDiagnosticSeverity severity;
    int line;
    int column;
    const char* message; /* owned by the ShaderCompilationResult */
} ShaderDiagnostic;

/**
 * Fetch a diagnostic entry by index (0-based, < ShaderResultGetDiagnosticCount).
 * Returns a diagnostic with a NULL message if index is out of range.
 */
ShaderDiagnostic ShaderResultGetDiagnostic(const ShaderCompilationResult* result,
                                            size_t index);

/* -------------------------------------------------------------------- */
/* Extensibility: custom high-level language features                   */
/* -------------------------------------------------------------------- */

/**
 * A transform is a function that runs over the AST after parsing and
 * before code generation. Bshader's built-in high-level features (like
 * `blur(...)`) are implemented as transforms; user code can register
 * additional ones through ShaderContextRegisterTransform() to add new
 * call-style language features without touching the parser.
 *
 * `functionName` is the identifier that triggers this transform when
 * seen as a call expression in a `material { }` block, e.g. "blur".
 *
 * The callback receives an opaque handle to the call-expression node
 * being expanded and an opaque handle to the enclosing builder; both are
 * only valid for the duration of the callback. See DOCS.md, section
 * "Extensibility Guide", for the full authoring contract and the
 * ShaderTransformApi used to inspect arguments and emit replacement
 * code.
 */
typedef struct ShaderTransformNode ShaderTransformNode;
typedef struct ShaderTransformBuilder ShaderTransformBuilder;

typedef void (*ShaderTransformFn)(ShaderTransformNode* node,
                                   ShaderTransformBuilder* builder,
                                   void* userData);

/**
 * Register a custom transform under the given function name. If a
 * transform (built-in or custom) is already registered under that name,
 * it is replaced. Registered transforms are cleared by
 * ShaderContextDestroy() but survive ShaderContextReset().
 */
void ShaderContextRegisterTransform(ShaderContext* ctx,
                                     const char* functionName,
                                     ShaderTransformFn fn,
                                     void* userData);

/* Argument accessors used inside a ShaderTransformFn -- see DOCS.md. */
size_t ShaderTransformNodeGetArgCount(const ShaderTransformNode* node);
const char* ShaderTransformNodeGetArgAsIdentifier(const ShaderTransformNode* node, size_t index);
double ShaderTransformNodeGetArgAsNumber(const ShaderTransformNode* node, size_t index);

/**
 * True if argument `index` is an identifier that refers to one of the
 * enclosing shader's declared `properties { }` entries (as opposed to a
 * local variable or a built-in such as UV). Property references must be
 * lowered through ShaderTransformNodeResolveIdentifier() rather than
 * emitted as a bare name, since the compiled output renames them.
 */
bool ShaderTransformNodeArgIsProperty(const ShaderTransformNode* node, size_t index);

/**
 * Resolve an identifier exactly the way the generator itself would: a
 * property name is rewritten to its compiled-output reference, anything
 * else (a local variable, a built-in like UV) is returned unchanged.
 * Use this instead of hand-formatting property names in a transform, so
 * transforms stay correct even if the internal naming convention for
 * properties ever changes. The returned pointer is valid until the next
 * call to this function on the same node.
 */
const char* ShaderTransformNodeResolveIdentifier(const ShaderTransformNode* node, const char* identifier);

/* Emission helpers used inside a ShaderTransformFn -- see DOCS.md. */

/**
 * Emit an extra statement into the generated code, immediately before
 * the statement that contained this call expression. Used for things
 * like declaring a temporary or invoking a helper with side effects.
 */
void ShaderTransformBuilderEmitLine(ShaderTransformBuilder* builder, const char* code);

/**
 * Request that a named helper function be made available in the
 * generated output. `helperName` must match a helper registered with
 * ShaderContextRegisterHelperSource(); the same helper is only ever
 * injected once per compilation no matter how many call sites request
 * it. See DOCS.md for the built-in helper used by `blur`.
 */
void ShaderTransformBuilderRequireHelper(ShaderTransformBuilder* builder, const char* helperName);

/**
 * Set the expression text that should replace the original call
 * expression at its use site (e.g. "sc_blur_sample(baseColor, UV, 2.0)").
 * Must be called exactly once per invocation of the transform. If never
 * called, the call expression is replaced with a safe zero-value default
 * and a warning diagnostic is recorded.
 */
void ShaderTransformBuilderSetReplacementExpr(ShaderTransformBuilder* builder, const char* exprText);

/**
 * Register the GLSL (or target-language) source of a named helper
 * function so that ShaderTransformBuilderRequireHelper() can pull it
 * into generated output on demand. Typically called once, right after
 * ShaderContextCreate(), alongside ShaderContextRegisterTransform().
 */
void ShaderContextRegisterHelperSource(ShaderContext* ctx, const char* helperName, const char* sourceCode);

#ifdef __cplusplus
}
#endif

#endif /* BSHADER_SHADER_TRANSLATOR_H */
