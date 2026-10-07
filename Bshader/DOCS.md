# Bshader

Bshader is a small, high-level shading language and a C library that
translates it into annotated Filament material source. It gives shader
authors a clean, modern surface (typed properties, ordinary control
flow, high-level helpers like `blur(...)`) without exposing anything
about how that surface is actually lowered under the hood — client code
only ever sees `shader_translator.h`.

This document covers three things:

For the maintained language reference, defaults, loops, vertex stages,
camera inputs, examples, and full C API, start at
[the public Bshader docs](../docs/public/0.5.0/bshader/en.html).
Translation alone is not GPU compilation: `Editor/materials.py` extracts
reflection, removes default metadata, lowers uniform references, and invokes
Filament `matc` to build a runtime-loadable `.filamat` package.

1. [Public API Guide](#1-public-api-guide) — how to call the library.
2. [Internal Architecture Overview](#2-internal-architecture-overview) — how the pieces fit together.
3. [Extensibility Guide](#3-extensibility-guide) — how to add a new language feature.

---

## 1. Public API Guide

Everything a caller needs lives in `include/shader_translator.h`. The
usual lifecycle is: create a context, compile one or more shaders,
inspect each result, free it, and eventually destroy the context.

### 1.1 Minimal example

```c
#include "shader_translator.h"
#include <stdio.h>

int main(void) {
    ShaderContext* ctx = ShaderContextCreate();

    const char* source =
        "shader MyShader {\n"
        "    properties {\n"
        "        baseColor: texture2d;\n"
        "        roughnessFactor: float = 0.5;\n"
        "    }\n"
        "    material {\n"
        "        color = texture(baseColor, UV);\n"
        "        roughness = roughnessFactor;\n"
        "    }\n"
        "}\n";

    ShaderCompilationResult* result = ShaderTranslatorCompileString(ctx, source);

    if (ShaderResultIsSuccess(result)) {
        /* Annotated source, NOT a renderer-loadable package.
         * Process reflection and uniforms, then compile with matc. */
        printf("%s\n", ShaderResultGetOutput(result));
    } else {
        fprintf(stderr, "compile failed: %s\n", ShaderResultGetError(result));
        for (size_t i = 0; i < ShaderResultGetDiagnosticCount(result); i++) {
            ShaderDiagnostic d = ShaderResultGetDiagnostic(result, i);
            fprintf(stderr, "  line %d: %s\n", d.line, d.message);
        }
    }

    ShaderCompilationResultFree(result);
    ShaderContextDestroy(ctx);
    return 0;
}
```

### 1.2 Lifecycle functions

| Function | Purpose |
|---|---|
| `ShaderContextCreate()` | Allocates a context and its internal arena. Call once per thread. |
| `ShaderContextReset(ctx)` | Frees memory used by previous compiles while keeping the context (and any custom transforms/helpers registered on it) usable. Cheaper than destroy+create in a loop. |
| `ShaderContextDestroy(ctx)` | Frees the context, its arena, and its transform registry. |

A `ShaderContext` is **not thread-safe**. Use one per thread, or add
your own locking.

### 1.3 Compiling

| Function | Purpose |
|---|---|
| `ShaderTranslatorCompile(ctx, src, size)` | Compile a buffer that may not be NUL-terminated. |
| `ShaderTranslatorCompileString(ctx, src)` | Convenience wrapper for a NUL-terminated C string. |
| `ShaderCompilationResultFree(result)` | Frees everything owned by a result. Always call this, whether or not compilation succeeded. |

Both compile functions **always** return a non-NULL result — check
`ShaderResultIsSuccess()` to know what happened. A `ShaderContext` can be
reused for many compiles; a `ShaderCompilationResult`, once produced,
stays valid independently of the context (including across
`ShaderContextReset()`/`ShaderContextDestroy()`) until you free it.

### 1.4 Inspecting a result

| Function | Purpose |
|---|---|
| `ShaderResultIsSuccess(result)` | Did it compile? |
| `ShaderResultGetOutput(result)` / `ShaderResultGetOutputSize(result)` | Annotated material source (NULL on failure), not GPU data. |
| `ShaderResultGetError(result)` | A one-line summary of the first error (NULL on success). |
| `ShaderResultGetDiagnosticCount(result)` / `ShaderResultGetDiagnostic(result, i)` | The full list of errors *and* warnings, each with a line/column and severity. |

Prefer walking the diagnostics list over parsing `ShaderResultGetError()`
if you want to show the user everything wrong with their shader at
once — the compiler does not stop at the first error where it can
usefully keep going (see `sc_synchronize()` in `src/parser.c`).

### 1.5 Memory ownership summary

- Everything returned by a `ShaderCompilationResult*` accessor
  (`ShaderResultGetOutput`, `ShaderResultGetError`, diagnostic messages)
  is owned by that result and freed by `ShaderCompilationResultFree()`.
  Don't `free()` it yourself; don't use it after freeing the result.
- Nothing returned by the context-level lifecycle functions needs
  freeing by hand beyond calling `ShaderContextDestroy()` once.

---

## 2. Internal Architecture Overview

```
source text
    |
    v
 +--------+   tokens    +--------+   AST     +-------------+  compiled text
 | Lexer  | ----------> | Parser | --------> |  Generator  | -------------->
 +--------+             +--------+           | (+ Transform|
                                              |   Registry) |
                                              +-------------+
```

| File | Role |
|---|---|
| `src/arena.c` / `arena.h` | Bump allocator backing all AST and diagnostic memory for one compile. Reset or freed as a single unit — no per-node `free()` calls anywhere in the parser or AST. |
| `src/lexer.c` / `lexer.h` | Hand-written lexer. Tokenizes identifiers, numbers, keywords, and operators; tracks line/column for diagnostics. |
| `src/ast.c` / `ast.h` | Plain-C struct definitions for every expression and statement kind, plus arena-backed constructors. |
| `src/parser.c` / `parser.h` | Hand-written **recursive descent** parser, one function per grammar rule (`sc_parse_expr` -> `sc_parse_logical_or` -> ... -> `sc_parse_primary`, `sc_parse_stmt`, `sc_parse_properties_block`, `sc_parse_shader_decl`). On a syntax error it records a diagnostic and calls `sc_synchronize()` to skip to the next statement/declaration boundary, so one typo doesn't cascade into a wall of errors. |
| `src/diag.c` / `diag.h` | Growable list of `{severity, line, column, message}` entries shared by every compilation stage. |
| `src/transform.c` / `transform_internal.h` | The extensible transform registry (see [section 3](#3-extensibility-guide)) plus the built-in `blur` feature. |
| `src/generator_filament.c` / `generator_filament.h` | Walks the AST and lowers it to the compiled payload. **This is the only file in the project that knows the target format** — the public API and every other internal file are agnostic to it. |
| `src/strbuf.c` / `strset.c` | Small malloc-backed utilities (growable string buffer, growable string set) used by the generator, kept separate from the arena because generator *output* must outlive the arena that produced the AST it was generated from. |
| `src/shader_translator.c` | Implements the public `ShaderContext` / `ShaderCompilationResult` types and wires lexer -> parser -> generator together behind `ShaderTranslatorCompile()`. |

### 2.1 Why an arena?

Every AST node, every diagnostic entry, and every intermediate string
produced while parsing is allocated from `ShaderContext`'s arena
(`src/arena.c`). This means:

- No per-node `free()` bookkeeping anywhere in the parser or AST code.
- `ShaderContextReset()` reclaims everything from previous compiles in
  one call.
- The only things allocated *outside* the arena are the final compiled
  output text and the `ShaderCompilationResult` itself, because those
  must be able to outlive the arena (a result stays valid even after
  `ShaderContextDestroy()`).

### 2.2 How `blur(...)` actually becomes GLSL

1. The parser sees `blur(baseColor, UV, 2.0)` as an ordinary call
   expression (`SC_EXPR_CALL` with `name == "blur"`) — the parser has
   **no special knowledge of `blur`** at all; any identifier followed by
   `(...)` parses the same way.
2. At code generation time (`sc_emit_call` in `generator_filament.c`),
   the generator looks up the call's name in the transform registry. It
   finds the built-in entry registered for `"blur"`.
3. For built-in blur, the generator lowers argument expressions recursively,
   preserving compound UV/radius expressions and stage aliases, then requests
   `sc_blur_sample`. Ordinary parameters use `materialParams.name`; samplers
   use `materialParams_name`.
4. For custom registered transforms (including overrides of blur), it builds a
   node and builder and invokes the callback. The callback must supply its own
   replacement and respect the documented literal/identifier argument contract.
5. The generator substitutes that replacement text wherever the
   original `blur(...)` call appeared, and injects the
   `sc_blur_sample` helper's source into the compiled fragment block
   exactly once, no matter how many call sites requested it.

The upshot: a shader author writes one line, `color = blur(baseColor, UV, 2.0);`,
and gets a real multi-tap blur; the *language* doesn't need a `blur`
keyword or any parser changes to support it.

---

## 3. Extensibility Guide

New high-level, call-style language features (`myFeature(...)`) can be
added **without touching the lexer, parser, or grammar at all** — you
just register a transform.

### 3.1 The contract

```c
typedef void (*ShaderTransformFn)(ShaderTransformNode* node,
                                   ShaderTransformBuilder* builder,
                                   void* userData);

void ShaderContextRegisterTransform(ShaderContext* ctx,
                                     const char* functionName,
                                     ShaderTransformFn fn,
                                     void* userData);

void ShaderContextRegisterHelperSource(ShaderContext* ctx,
                                        const char* helperName,
                                        const char* sourceCode);
```

Whenever the generator encounters a call expression whose callee name
matches `functionName`, it invokes your `fn` instead of emitting an
ordinary function call. Inside `fn`, you:

1. **Read arguments** off `node`:
   - `ShaderTransformNodeGetArgCount(node)`
   - `ShaderTransformNodeGetArgAsIdentifier(node, index)` — NULL if that argument isn't a plain identifier.
   - `ShaderTransformNodeGetArgAsNumber(node, index)` — `0.0` if that argument isn't a numeric literal.
   - `ShaderTransformNodeArgIsProperty(node, index)` / `ShaderTransformNodeResolveIdentifier(node, name)` — use these instead of hand-rolling the `materialParams_` naming convention, so your transform keeps working even if that convention ever changes.
2. **Emit code** through `builder`:
   - `ShaderTransformBuilderEmitLine(builder, code)` — adds an extra statement immediately before the statement that contained your call (e.g. to declare a temporary).
   - `ShaderTransformBuilderRequireHelper(builder, helperName)` — requests that a named helper function (registered separately, see below) be included in the compiled output. Safe to call redundantly from many call sites; each helper is only injected once per compile.
   - `ShaderTransformBuilderSetReplacementExpr(builder, exprText)` — **required**: the text that replaces your call expression at its use site. Missing replacement or requested helper source fails translation.

If your feature needs supporting code (a helper function, a constant
table, etc.), register its source once with
`ShaderContextRegisterHelperSource()` — typically right next to where
you call `ShaderContextRegisterTransform()` — and request it from
inside your transform with `ShaderTransformBuilderRequireHelper()`.

### 3.2 Worked example: a `desaturate(color, amount)` feature

```c
static void desaturate_transform(ShaderTransformNode* node,
                                  ShaderTransformBuilder* builder,
                                  void* userData) {
    (void)userData;

    const char* color_arg = ShaderTransformNodeGetArgAsIdentifier(node, 0);
    const char* color = color_arg ? ShaderTransformNodeResolveIdentifier(node, color_arg) : "vec3(0.0)";
    double amount = ShaderTransformNodeGetArgAsNumber(node, 1);

    ShaderTransformBuilderRequireHelper(builder, "sc_desaturate");

    char expr[256];
    snprintf(expr, sizeof(expr), "sc_desaturate(%s, %.9f)", color, amount);
    ShaderTransformBuilderSetReplacementExpr(builder, expr);
}

static const char* DESATURATE_HELPER =
    "vec3 sc_desaturate(vec3 c, float amount) {\n"
    "    float gray = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    return mix(c, vec3(gray), amount);\n"
    "}\n";

/* Somewhere during setup, right after ShaderContextCreate(): */
ShaderContextRegisterTransform(ctx, "desaturate", desaturate_transform, NULL);
ShaderContextRegisterHelperSource(ctx, "sc_desaturate", DESATURATE_HELPER);
```

With this registered, a Bshader author can now write:

```glsl
vec3 baseColorSample = vec3(1.0, 0.2, 0.1);
color = vec4(desaturate(baseColorSample, 0.5), 1.0);
```

and it will expand exactly the way `blur(...)` does — no parser or
grammar changes required.

### 3.3 Adding a brand-new *syntax* construct (not just a call)

Transforms cover anything expressible as `name(args...)`. If you need
genuinely new syntax (a new keyword, a new statement shape, a new
operator), that does require touching the front end. The places to
change are:

1. **`src/lexer.h` / `lexer.c`** — add a new keyword to `SC_KEYWORDS` and a matching `sc_token_kind` enumerator if your feature needs a new reserved word.
2. **`src/ast.h` / `ast.c`** — add a new `sc_expr_kind` or `sc_stmt_kind` variant (and a constructor function) to represent it.
3. **`src/parser.c`** — add a case to `sc_parse_stmt` (for a new statement form) or a new precedence level / branch in the expression chain (for a new operator), producing your new AST node.
4. **`src/generator_filament.c`** — add a case to `sc_emit_stmt` or `sc_emit_expr` that lowers your new node to output text.

Keep new grammar rules as small, single-purpose functions following the
existing naming convention (`sc_parse_<thing>`), and keep the four
concerns above (lex, parse, represent, generate) in their own files,
the same way `blur` and every existing construct does.

### 3.4 Implemented capabilities and remaining boundaries

The earlier limitations are resolved: locals have lexical block scope,
ordinary properties use `materialParams.name`, samplers remain separate,
and vector/matrix properties support constant defaults. Vertex blocks, camera
inputs, bounded loops, swizzle/index assignments, and render options are also
supported. Native tests and real matc tests cover these paths.

Remaining deliberate boundaries include no user function definitions, arrays,
structs, custom varyings, compute stages, or preprocessor. Callback argument
accessors accept identifiers/literals, not arbitrary expression serialization.
See the public language/compiler references for limits, stages, and ownership.
