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
    "        vec4 finalColor = texture(baseColor, UV);\n"
    "        if (useBlur) {\n"
    "            finalColor = blur(baseColor, UV, 2.0);\n"
    "        }\n"
    "        color = finalColor;\n"
    "        roughness = roughnessFactor;\n"
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
        CHECK(strstr(out, "materialParams.roughnessFactor") != NULL,
              "non-sampler properties use Filament's parameter struct");
        CHECK(strstr(out,"materialParams_baseColor")!=NULL,"samplers remain separate uniforms");
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

static void test_language_extensions(void) {
    SECTION("defaults, stage inputs, bounded loops and validation");
    ShaderContext* ctx=ShaderContextCreate();
    const char* valid="shader Advanced { options { shading:unlit; blending:transparent; doubleSided:true; } properties { tint:vec4=vec4(.5,1e-1,0.0,1.0); matrix:mat4=mat4(1.0); } vertex { position=position+vec3(sin(time)); UV=UV*2.0; } material { vec4 value=matrix*tint; for(int i=0;i<4;i++){value[i]=value[i]*.5;continue;} int n=0; while(n<2){n++;break;} color=value; } }";
    ShaderCompilationResult* result=ShaderTranslatorCompileString(ctx,valid);
    CHECK(ShaderResultIsSuccess(result),"extended language should translate");
    if(ShaderResultIsSuccess(result)) {
        const char* out=ShaderResultGetOutput(result);
        CHECK(strstr(out,"materialVertex")!=NULL,"vertex entry generated");
        CHECK(strstr(out,"1024")!=NULL,"loops contain an iteration guard");
        CHECK(strstr(out,"getUserTime().x")!=NULL,"time is elapsed render time");
        CHECK(strstr(out,"default : [")!=NULL,"vector and matrix defaults are reflected");
        CHECK(strstr(out,"shadingModel : unlit")!=NULL,"unlit is a real shading model");
    }
    ShaderCompilationResultFree(result);ShaderContextReset(ctx);
    const char* invalid[]={
        "shader X { properties { a:vec3=vec3(1.0,2.0); } material {color=vec4(1.0);} }",
        "shader X { properties { a:float=1.0; } material {a=2.0;} }",
        "shader X { properties { a:vec4; } material {a.x=2.0;} }",
        "shader X { properties { a:float; a:int; } material {} }",
        "shader X { material {if(true){float hidden=1.0;} roughness=hidden;} }",
        "shader X { material {float n=1.0; float n=2.0;} }",
        "shader X { material {break;} }",
        "shader X { options {shading:unlit;} material {roughness=.5;} }",
        "shader X { vertex {color=vec4(1.0);} material {} }",
        "shader X { material {color=modelMatrix*vec4(1.0);} }",
        "shader X { material {} } garbage",
        "shader X { material {} } /* unfinished",
        "shader X { properties {a:float=1e999;} material {} }",
        "shader X { properties {a:int=2147483648;} material {} }",
        "shader X { options {shading:unknown;} material {} }",
        "shader X { material { color=blur(); } }"
        ,"shader X { material {for(int ;true;missing++) {}} }"
        ,"shader X { material {for(int i;i<2;i++) {}} }"
    };
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        result=ShaderTranslatorCompileString(ctx,invalid[i]);
        CHECK(!ShaderResultIsSuccess(result),invalid[i]);
        CHECK(ShaderResultGetDiagnosticCount(result)>0,"invalid input provides diagnostics");
        ShaderCompilationResultFree(result);ShaderContextReset(ctx);
    }
    result=ShaderTranslatorCompileString(ctx,"shader X {material {float a=1.0;if(true){float a=2.0;roughness=a;}roughness=a;}} ");
    CHECK(ShaderResultIsSuccess(result),"inner scope may shadow outer locals");
    ShaderCompilationResultFree(result);ShaderContextDestroy(ctx);
}

int main(void) {
    test_compiles_blur_shader();
    test_reports_syntax_errors();
    test_reports_undeclared_identifier();
    test_custom_transform();
    test_context_reuse();
    test_language_extensions();

    printf("\n%d/%d tests passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
