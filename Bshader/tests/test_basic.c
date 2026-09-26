/* test_basic.c -- lightweight test harness for the Bshader compiler.
 * No external test framework: plain asserts + a summary at the end. */

#include "shader_translator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        g_tests_run++;                                                       \
        if (!(cond)) {                                                       \
            g_tests_failed++;                                                \
            fprintf(stderr, "  FAILED: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                                     \
    } while (0)

#define SECTION(name) printf("-- %s --\n", name)

/* ------------------------------------------------------------------ */
/* 1. A well-formed shader with the blur() high-level feature compiles */
/* ------------------------------------------------------------------ */

static const char* SOURCE_BLUR =
    "shader MyShader {\n"
    "    properties {\n"
    "        baseColor: texture2d;\n"
    "        roughnessFactor: float = 0.5;\n"
    "        useBlur: bool = false;\n"
    "    }\n"
    "    material {\n"
    "        float finalRoughness = roughnessFactor;\n"
    "        if (useBlur) {\n"
    "            finalRoughness = blur(baseColor, UV, 2.0);\n"
    "        }\n"
    "        color = texture(baseColor, UV);\n"
    "        roughness = finalRoughness;\n"
    "    }\n"
    "}\n";

static void test_compiles_blur_shader(void) {
    SECTION("compiles a shader using the blur() feature");

    ShaderContext* ctx = ShaderContextCreate();
    CHECK(ctx != NULL, "ShaderContextCreate should not return NULL");

    ShaderCompilationResult* result = ShaderTranslatorCompileString(ctx, SOURCE_BLUR);
    CHECK(result != NULL, "compile should always return a non-NULL result");
    CHECK(ShaderResultIsSuccess(result), "well-formed source should compile successfully");

    if (ShaderResultIsSuccess(result)) {
        const char* out = ShaderResultGetOutput(result);
        CHECK(out != NULL, "successful result should have output");
        CHECK(strstr(out, "material {") != NULL, "output should contain a material block");
        CHECK(strstr(out, "fragment {") != NULL, "output should contain a fragment block");
        /* The blur() call must have been expanded to the helper, never
         * left as a literal "blur(" call in the compiled output -- that
         * is the whole point of the transform pipeline. */
        CHECK(strstr(out, "blur(") == NULL, "raw blur() call should not appear in output");
        CHECK(strstr(out, "sc_blur_sample") != NULL, "blur() should expand to the sampling helper");
        CHECK(strstr(out, "materialParams_roughnessFactor") != NULL,
              "property references should be lowered to materialParams_<name>");
    }

    ShaderCompilationResultFree(result);
    ShaderContextDestroy(ctx);
}

/* ------------------------------------------------------------------ */
/* 2. Syntax errors are reported, not crashed on                       */
/* ------------------------------------------------------------------ */

static void test_reports_syntax_errors(void) {
    SECTION("reports syntax errors instead of crashing");

    const char* bad_source =
        "shader Broken {\n"
        "    material {\n"
        "        color = ;\n" /* missing expression */
        "    }\n"
        "}\n";

    ShaderContext* ctx = ShaderContextCreate();
    ShaderCompilationResult* result = ShaderTranslatorCompileString(ctx, bad_source);

    CHECK(result != NULL, "compile should return a result even for bad input");
    CHECK(!ShaderResultIsSuccess(result), "malformed source should not report success");
    CHECK(ShaderResultGetOutput(result) == NULL, "failed compile should have no output");
    CHECK(ShaderResultGetError(result) != NULL, "failed compile should have an error message");
    CHECK(ShaderResultGetDiagnosticCount(result) > 0, "failed compile should have at least one diagnostic");

    ShaderCompilationResultFree(result);
    ShaderContextDestroy(ctx);
}

/* ------------------------------------------------------------------ */
/* 3. Semantic errors: assigning to an undeclared identifier            */
/* ------------------------------------------------------------------ */

static void test_reports_undeclared_identifier(void) {
    SECTION("reports assignment to an undeclared variable");

    const char* bad_source =
        "shader Broken {\n"
        "    material {\n"
        "        notARealField = 1.0;\n"
        "    }\n"
        "}\n";

    ShaderContext* ctx = ShaderContextCreate();
    ShaderCompilationResult* result = ShaderTranslatorCompileString(ctx, bad_source);

    CHECK(!ShaderResultIsSuccess(result), "assigning to an unknown target should fail compilation");

    int found_expected_message = 0;
    for (size_t i = 0; i < ShaderResultGetDiagnosticCount(result); i++) {
        ShaderDiagnostic diag = ShaderResultGetDiagnostic(result, i);
        if (diag.message && strstr(diag.message, "notARealField")) {
            found_expected_message = 1;
        }
    }
    CHECK(found_expected_message, "diagnostics should mention the offending identifier");

    ShaderCompilationResultFree(result);
    ShaderContextDestroy(ctx);
}

/* ------------------------------------------------------------------ */
/* 4. A custom, user-registered transform works end-to-end              */
/* ------------------------------------------------------------------ */

static void custom_double_transform(ShaderTransformNode* node, ShaderTransformBuilder* builder, void* userData) {
    (void)userData;
    double value = ShaderTransformNodeGetArgAsNumber(node, 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value * 2.0);
    ShaderTransformBuilderSetReplacementExpr(builder, buf);
}

static void test_custom_transform(void) {
    SECTION("supports registering a custom transform");

    const char* source =
        "shader Custom {\n"
        "    material {\n"
        "        float x = double(21.0);\n"
        "        roughness = x;\n"
        "    }\n"
        "}\n";

    ShaderContext* ctx = ShaderContextCreate();
    ShaderContextRegisterTransform(ctx, "double", custom_double_transform, NULL);

    ShaderCompilationResult* result = ShaderTranslatorCompileString(ctx, source);
    CHECK(ShaderResultIsSuccess(result), "shader using a registered custom transform should compile");

    if (ShaderResultIsSuccess(result)) {
        const char* out = ShaderResultGetOutput(result);
        CHECK(strstr(out, "= 42") != NULL, "custom transform should have expanded double(21.0) to 42");
    }

    ShaderCompilationResultFree(result);
    ShaderContextDestroy(ctx);
}

/* ------------------------------------------------------------------ */
/* 5. ShaderContextReset allows reusing a context across compiles       */
/* ------------------------------------------------------------------ */

static void test_context_reuse(void) {
    SECTION("supports reusing a context across multiple compiles");

    ShaderContext* ctx = ShaderContextCreate();

    for (int i = 0; i < 3; i++) {
        ShaderCompilationResult* result = ShaderTranslatorCompileString(ctx, SOURCE_BLUR);
        CHECK(ShaderResultIsSuccess(result), "repeated compiles on a reset context should succeed");
        ShaderCompilationResultFree(result);
        ShaderContextReset(ctx);
    }

    ShaderContextDestroy(ctx);
}

int main(void) {
    test_compiles_blur_shader();
    test_reports_syntax_errors();
    test_reports_undeclared_identifier();
    test_custom_transform();
    test_context_reuse();

    printf("\n%d/%d tests passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
