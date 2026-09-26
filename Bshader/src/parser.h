/* parser.h -- hand-written recursive-descent parser for Bshader. Internal. */
#ifndef BSHADER_PARSER_H
#define BSHADER_PARSER_H

#include "arena.h"
#include "ast.h"
#include "diag.h"

/**
 * Parse a full Bshader source buffer into a program AST.
 *
 * Returns a non-NULL sc_program on success. On unrecoverable syntax
 * errors, still returns a non-NULL sc_program (possibly with a NULL
 * `shader` field) so callers can uniformly check diagnostics rather than
 * NULL; check sc_diag_has_errors(diags) to determine real success.
 *
 * All AST memory is allocated from `arena` and lives as long as it does.
 */
sc_program* sc_parse_program(sc_arena* arena, sc_diag_list* diags,
                              const char* src, size_t length);

#endif /* BSHADER_PARSER_H */
