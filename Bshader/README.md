# Bshader

A high-level shading language and C compiler library. See
[`DOCS.md`](DOCS.md) for the full public API guide, internal
architecture overview, and extensibility guide.

## Building

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

This produces `libbshader.a` (a static library by default; pass
`-DBSHADER_BUILD_SHARED=ON` for a shared library instead) and a test
executable, `bshader_tests`.

## Running the tests

```sh
cd build
./bshader_tests
# or: ctest
```

## Layout

```
include/shader_translator.h   Public API (the only header client code needs)
src/                          Lexer, parser, AST, transform registry, generator
examples/example.bshader      A sample shader using properties + blur()
tests/test_basic.c            Test harness (success cases, error cases,
                               custom transform registration, context reuse)
```
