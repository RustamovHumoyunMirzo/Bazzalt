# Third-party dependencies and notices

BAZZALT's module licenses do not replace upstream licenses. This inventory covers
direct source dependencies, linked Filament SDK libraries, Python dependencies and
distribution tools. A dependency is only part of a product when that product
actually includes it. Versions are pinned in fetch scripts, CMake, workflows or
`requirements-build.txt`; Python package metadata is authoritative for the installed
version and its bundled libraries.

Release payloads retain full license texts in `licenses/`, alongside upstream
package notices. Preserve these files when redistributing a runtime. Source URLs
below identify upstream notices; they are not a substitute for the license text.
Pinned Filament/Qt notice snapshots, including full LGPL/GPL texts, are available
offline in [licenses/third_party](licenses/third_party); their source URLs and hashes
are in [sources.json](licenses/third_party/sources.json).
No system font is redistributed. Games must obtain distribution rights for their
fonts, audio, textures and other project content.

Local compatibility patches preserve upstream licenses. Sol2 3.3.1's member
callbacks omit their dependent `noexcept` specification only on Clang 18 to
work around [upstream issue #1581](https://github.com/ThePhD/sol2/issues/1581).
The Linux x86 Filament build adds i386 position-independent BlueGL dispatch
thunks; upstream x64 and ARM assembly remains unchanged. Patch sources and
regression probes are in `scripts/patch_*` and `tests/Sol2CompatibilityProbe.cpp`.
The x86 patch also limits libc++ mutex annotations to libc++ builds; it leaves
mutex operations and unrelated diagnostics unchanged.

## Core, GUI and scripting

| Dependency / copyright owner | Used for | License and upstream notice |
| --- | --- | --- |
| [EnTT](https://github.com/skypjack/entt), Michele Caini and contributors | ECS | [MIT](https://github.com/skypjack/entt/blob/master/LICENSE) |
| [Filament](https://github.com/google/filament), Google and contributors | Rendering, gltfio, filameshio, filamat, matc, cmgen | [Apache-2.0](https://github.com/google/filament/blob/main/LICENSE); retain its third-party notices too |
| [rapidyaml](https://github.com/biojppm/rapidyaml), Joao Paulo Magalhaes | YAML | [MIT](https://github.com/biojppm/rapidyaml/blob/master/LICENSE.txt) |
| [c4core](https://github.com/biojppm/c4core), Joao Paulo Magalhaes | rapidyaml's utility dependency | [MIT](https://github.com/biojppm/c4core/blob/master/LICENSE.txt) |
| [Lua](https://www.lua.org), Lua.org / PUC-Rio | Separate scripting library | [MIT](https://www.lua.org/license.html) |
| [sol2](https://github.com/ThePhD/sol2), Rapptz, ThePhD and contributors | Lua bindings | [MIT](https://github.com/ThePhD/sol2/blob/develop/LICENSE.txt) |
| [SDL3](https://github.com/libsdl-org/SDL), Sam Lantinga and contributors | Windowing/input | [zlib](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt) |
| [miniaudio](https://github.com/mackron/miniaudio), David Reid | Audio decoding/mixing/output | [Public-domain or MIT-0 alternatives](https://github.com/mackron/miniaudio/blob/master/LICENSE); MIT-0 is the distribution option used here |
| [stb](https://github.com/nothings/stb), Sean Barrett and contributors | Image decoding through Filament, Vorbis decoding with miniaudio | [MIT or public-domain alternatives](https://github.com/nothings/stb/blob/master/LICENSE); preserve file notices |
| [FreeType](https://freetype.org), FreeType Project contributors | Private GUI rasterization | [FreeType License](https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT) (chosen alternative to GPL); Portions of this software are copyright © 2026 The FreeType Project (www.freetype.org). All rights reserved. |
| zlib contributors | FreeType's bundled gzip/zlib implementation | [zlib notice in source](https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/src/gzip/zlib.h) |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz), Google and other named authors | Private GUI OpenType shaping | [Old MIT and per-file notices](https://github.com/harfbuzz/harfbuzz/blob/main/COPYING) |
| [SheenBidi](https://github.com/Tehreer/SheenBidi), Muhammad Tayyab Akram | Unicode bidirectional layout | [Apache-2.0](https://github.com/Tehreer/SheenBidi/blob/master/LICENSE) |

## Prepared optional dependencies

Jolt Physics 5.3.0, Jorrit Rouwe and contributors, is pinned for future physics
integration under the [MIT license](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/LICENSE).
It is not linked into or required by current engine/editor/game distributions.
Its notice is preserved in `licenses/third_party/Jolt/LICENSE`; the dependency
collector also copies `deps/jolt/LICENSE` when source is present.

## Filament SDK dependencies

Filament ships code from other projects. Its SDK can contain more than the
libraries linked by BAZZALT. Keep the SDK's complete notices with a distribution;
the following identifies the libraries linked by `cmake/Filament.cmake`, rather
than relicensing them under Filament's Apache license.

| Project | License / source notice |
| --- | --- |
| Draco (`dracodec`) | [Apache-2.0](https://github.com/google/draco/blob/main/LICENSE) |
| meshoptimizer | [MIT](https://github.com/zeux/meshoptimizer/blob/master/LICENSE.md) |
| cgltf and jsmn (glTF/JSON parsing) | [cgltf MIT notice](licenses/third_party/Filament/third_party/cgltf/LICENSE), [jsmn MIT notice](licenses/third_party/Filament/third_party/jsmn/LICENSE) |
| robin-map (hash tables) | [MIT](licenses/third_party/Filament/third_party/robin-map/LICENSE) |
| Vulkan Memory Allocator | [MIT](licenses/third_party/Filament/third_party/vkmemalloc/LICENSE.txt) |
| TinyEXR and its image-tool dependencies | [BSD-3-Clause and bundled notices](licenses/third_party/Filament/third_party/tinyexr/NOTICE) |
| libpng, libwebp and zlib in image tooling | [libpng notice](licenses/third_party/Filament/third_party/libpng/LICENSE), [WebP BSD notice](licenses/third_party/Filament/third_party/libwebp/COPYING), [zlib notice](licenses/third_party/Filament/third_party/libz/LICENSE) |
| MikkTSpace | [zlib](https://github.com/mmikk/MikkTSpace/blob/master/mikktspace.h) |
| Basis Universal (`basis_transcoder`) | [Apache-2.0](https://github.com/BinomialLLC/basis_universal/blob/master/LICENSE) |
| smol-v | [Public domain / MIT alternatives](https://github.com/aras-p/smol-v/blob/master/LICENSE) |
| Zstandard (`zstd`, `uberzlib`) | [BSD-3-Clause or GPL-2.0 alternative](https://github.com/facebook/zstd/blob/dev/LICENSE); BSD distribution option |
| Vulkan headers/loader integration (`bluevk`) | [Apache-2.0 and MIT file notices](https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.md) |
| SPIR-V headers/tools and glslang in shader tooling | [Khronos notices](https://github.com/KhronosGroup/SPIRV-Tools/blob/main/LICENSE), [glslang per-file licenses](https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt) |
| SPIRV-Cross in shader tooling | [Apache-2.0](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/LICENSE) |
| Google/Filament libraries (`backend`, `bluegl`, `filabridge`, `filaflat`, `utils`, `geometry`, `image`, `imageio-lite`, `ibl-lite`, `ktxreader`, `shaders`, `matp`, `uberarchive`) | Filament [Apache-2.0](https://github.com/google/filament/blob/main/LICENSE), plus retained per-file third-party notices |

The snapshot also retains notices for SDK tools, tests and alternative backends:
Abseil, Dawn, CivetWeb, Dear ImGui, Assimp, GoogleTest/Benchmark, SDL2, getopt,
gl-matrix, Markdeep and upstream sample models. These are not implied to be linked
into every BAZZALT product. Their exact per-file licenses are in the
[snapshot inventory](licenses/third_party/sources.json).

## Editor, Hub and content compiler

| Dependency | License / upstream notice |
| --- | --- |
| CPython | [Python Software Foundation License and bundled notices](https://docs.python.org/3/license.html) |
| PySide6 and Shiboken6 (Qt for Python) | [LGPL-3.0/GPL/commercial options and notices](https://doc.qt.io/qtforpython-6/licenses.html); LGPL distribution uses the dynamically loaded libraries and corresponding source/relinking provisions |
| Qt modules/plugins included by PySide6 | [Module-specific licenses and third-party notices](https://doc.qt.io/qt-6/licenses-used-in-qt.html); do not assume all Qt modules have identical terms |
| PyYAML | [MIT](https://github.com/yaml/pyyaml/blob/main/LICENSE), with LibYAML's [MIT notice](https://github.com/yaml/libyaml/blob/master/License) when included |
| cryptography | [Apache-2.0 or BSD-3-Clause](https://github.com/pyca/cryptography/blob/main/LICENSE); retain bundled OpenSSL/Rust dependency notices from its wheel |
| OpenSSL bundled with Python/cryptography | [Apache-2.0 for OpenSSL 3](https://github.com/openssl/openssl/blob/master/LICENSE.txt); older distributions have different terms |
| cffi / pycparser, where installed by cryptography | [MIT](https://github.com/python-cffi/cffi/blob/main/LICENSE), [BSD-3-Clause](https://github.com/eliben/pycparser/blob/main/LICENSE) |
| Python zstandard | [BSD](https://github.com/indygreg/python-zstandard/blob/main/LICENSE), plus Zstandard's notice |
| ordered-set | [MIT](https://github.com/rspeer/ordered-set/blob/master/MIT-LICENSE) |

## Build and development tools

| Tool | License / upstream notice |
| --- | --- |
| pybind11 | [BSD-3-Clause](https://github.com/pybind/pybind11/blob/master/LICENSE) |
| Nuitka | [Apache-2.0](https://github.com/Nuitka/Nuitka/blob/develop/LICENSE.txt); preserve notices for its bundled support code |
| setuptools / packaging dependencies | [MIT](https://github.com/pypa/setuptools/blob/main/LICENSE), [packaging notices](https://github.com/pypa/packaging/blob/main/LICENSE) |
| LLVM/Clang, clang-cl, lld | [Apache-2.0 with LLVM exceptions](https://github.com/llvm/llvm-project/blob/main/llvm/LICENSE.TXT); deployed script toolchains keep their upstream licenses |
| CMake | [BSD-3-Clause](https://gitlab.kitware.com/cmake/cmake/-/blob/master/Copyright.txt) |
| Ninja | [Apache-2.0](https://github.com/ninja-build/ninja/blob/master/COPYING) |
| Inno Setup | [Inno Setup license](https://github.com/jrsoftware/issrc/blob/main/license.txt) |
| Microsoft Visual C++ runtime / Windows SDK | Microsoft distribution terms; only approved redistributable runtime files accompany Windows products, not the SDK |

GitHub Actions use actions/checkout, actions/setup-python, actions/cache and
upload/download-artifact under their respective repository licenses; these are
CI services rather than libraries in games. The OS graphics/audio drivers are not
redistributed by this project. Tests may use installed system fonts; those fonts
are not copied into release payloads.

## Maintaining notices

When adding or updating a dependency, update this file, its pinned source/version,
and release staging together. Preserve copyright/NOTICE files and any bundled
dependency licenses. Audit the actual packaged Qt plugins and Python wheel
metadata, not just the direct requirements list. A game containing only the native
runtime should carry runtime notices; it should not acquire Python/Qt merely to
make license collection convenient. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Linux system dependencies

The Linux player links the system OpenSSL libcrypto for SHA-256, HMAC/HKDF,
AES-256-GCM and ECDSA verification ([Apache-2.0 license](https://github.com/openssl/openssl/blob/openssl-3.0/LICENSE.txt)).
Linux presentation uses the system X11 libraries (MIT/X11 licenses) through SDL3.
These system libraries are installed by the distribution package manager, not
embedded into the core. Zstandard, already included in the Filament SDK notices,
also supplies portable Linux BPAK compression.
