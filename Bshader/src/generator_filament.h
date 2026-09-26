/* generator_filament.h -- lowers a Bshader AST into a compiled material
 * payload. Internal, and deliberately the *only* file in this project
 * that knows the target is Filament's .mat text format: nothing about
 * that leaks into shader_translator.h or any other public surface, per
 * the encapsulation requirement in DOCS.md. */
#ifndef BSHADER_GENERATOR_FILAMENT_H
#define BSHADER_GENERATOR_FILAMENT_H

#include "ast.h"
#include "diag.h"
#include "transform_internal.h"
#include <stddef.h>

/**
 * Generate the final compiled payload for `program`.
 *
 * May append additional diagnostics to `diags` for semantic problems
 * found only at generation time (undefined identifiers, output fields
 * assigned without a value reaching them, etc). Callers must re-check
 * sc_diag_has_errors(diags) after calling this even if it was already
 * clean going in.
 *
 * Returns a malloc'd, NUL-terminated buffer (also written to
 * *out_length, excluding the NUL) that the caller owns, or NULL if
 * generation could not proceed at all (e.g. program->shader is NULL).
 * The buffer is intentionally NOT arena-allocated: it must outlive the
 * ShaderContext's arena (see ShaderCompilationResult's lifetime
 * contract in shader_translator.h).
 */
char* sc_generate_filament_material(const sc_program* program,
                                     const sc_transform_registry* registry,
                                     sc_diag_list* diags,
                                     size_t* out_length);

#endif /* BSHADER_GENERATOR_FILAMENT_H */
