# Materials and shaders

## Editor workflow

Create a **Shader** (`.shad`) or **Filament Shader** (`.mat`) in the Asset Browser,
or import an existing source. Create a **Material** (`.matinst`) and select it.
The Inspector's Shader picker accepts only registered `.mat` and `.shad` assets.
Picking or dropping a shader rebuilds the parameter fields from its declarations.

Fill the fields, then pick or drop the material into **Mesh → Material Asset**.
This picker accepts only `.matinst`, not shaders or internal compiled packages.
The Mesh Asset must also be assigned. Clearing Material Asset restores the
model's imported materials. A material without a shader is valid but supplies no
rendering override.

Model instantiation creates individual entities for glTF nodes. Material overrides
belong to each entity's Mesh component: assigning a material to a parent does not
assign it to descendants. The legacy `Mesh.Materials` vector supplies primitive
slots when `MaterialAsset` is empty; a nonzero `MaterialAsset` overrides every
primitive of that entity's mesh. Imported glTF materials remain the fallback.

## Source and persistence

`.matinst` is versioned JSON. References are asset UUIDs, never filenames or
renderer handles. Moving or renaming a source together with its `.meta` keeps
references stable. Inspector edits are saved atomically to the material asset.
Unknown JSON fields are preserved when editing parameters. Switching shaders
intentionally resets the parameter set to the new shader's defaults.

```json
{
  "version": 1,
  "shader": "12345678-1234-4234-8234-123456789abc",
  "properties": {
    "roughness": 0.6,
    "tint": [0.7, 0.8, 1.0, 1.0],
    "albedo": "00000000-0000-0000-0000-000000000000"
  }
}
```

Supported reflected parameters are `float`, `float2`, `float3`, `float4`, `int`,
`bool`, `sampler2d`, `mat3`, and `mat4`. Their fields are numeric inputs, vector
inputs, checkboxes, project-image pickers, and matrix grids respectively.
Matrices are column-major. Texture pickers accept PNG/JPEG assets supported by
the standalone runtime texture loader. Unsupported types and uniform arrays
produce explicit diagnostics rather than unsafe calls into Filament. Additional
parameter families can extend the versioned reflection format.

Filament source parameters normally have no defaults: unspecified scalars and
vectors start at zero, matrices at identity, textures at None. Bshader scalar
defaults are retained in reflection and in new material instances.

Example `.shad`:

```text
shader Surface {
    properties {
        roughnessFactor: float = 0.5;
        tint: vec4;
    }
    material {
        color = tint;
        roughness = roughnessFactor;
    }
}
```

Use the equivalent direct Filament `.mat` syntax when target-specific features
are needed. Source diagnostics appear in the Console under Material Compiler.

## Compilation boundary

Import copies source; it does not compile every imported shader. Inspecting a
shader or material writes reflection and, for `.shad`, translated Filament
source. Compilation happens when a loaded scene's Mesh uses the material, or
an enabled attached script references a Material/Shader property. Play prepares
these dependencies before creating script instances. Live used-source changes
are also compiled for enabled camera custom post-processing effects. Changes
are checked during editor ticks. Value-only material edits do not recompile the
shader; source changes and compiler changes invalidate its compilation cache.

Artifacts are private asset-cache sidecars:

- `.reflection.json`: versioned parameter names, types, defaults and requirements.
- `.translated.mat`: compiler input, including the direct `.mat` path.
- `.filamat`: internal GPU package.
- `.build.json`: compilation fingerprint.

Bshader is built as `bshad.dll`/`libbshad.so`/`libbshad.dylib` **only for the
editor bridge build**. Python loads its C API; the engine never links it.
The editor normalizes Bshader target conventions to current Filament syntax.
The production bundle ships the translator and `tools/filament/matc` with the
editor. Runtime rendering consumes packages and material definitions only; game
code does not run a compiler or control the renderer.

## Public C++ API

```cpp
#include <Bazzalt/Material.h>
#include <Bazzalt/Shader.h>
#include <Bazzalt/Components/Mesh.h>

auto material = Bazzalt::Material::Load(materialUUID); // shared asset reference
auto instance = material.Instantiate();              // independent runtime copy
instance.SetColor("tint", {1, 0.4f, 0.2f, 1});
instance.SetFloat("roughnessFactor", 0.2f);
entity.GetComponent<Bazzalt::Mesh>().SetMaterial(instance);
```

`Material` supports `Load`, `Create(Shader)`, `Instantiate`, `IsValid`,
`GetAssetUUID`, `GetShader`, `GetParameters`, `HasParameter`, and typed setters
and getters for Float, Vec2/3/4, Color, Integer, Boolean, Texture, Matrix3/4.
Setters validate declared types and finite numbers; invalid parameters throw
`std::invalid_argument`. Color is an alias for float4, not a separate shader type.
Texture values are UUIDs. A zero UUID is None.

`Shader` supports `Load`, `IsValid`, `GetAssetUUID`, and `GetParameters`.
Each `ShaderParameter` has a Name and `ShaderParameterType`.
`Material::Create(shader)` starts a transient instance with shader defaults.
Loading the same material UUID shares runtime overrides; call Instantiate for
per-object isolation. Runtime changes do not save source assets. Stop clears
runtime overrides and transient instances; project teardown releases definitions
and renderer teardown releases all GPU instances and packages.

Serialized behavior fields use the same handles:

```cpp
#include <Bazzalt/Script.h>
#include <Bazzalt/Material.h>

COMPONENT(SurfaceController) {
public:
    PROPERTY(Bazzalt::Material, Surface, {})
    PROPERTY(Bazzalt::Shader, SurfaceShader, {})

    void OnUpdate(float) override {
        if (Surface.IsValid() && Surface.HasParameter("roughnessFactor"))
            Surface.SetFloat("roughnessFactor", 0.25f);
    }
};
```

The Inspector renders typed project pickers and stores UUIDs in the script
attachment manifest. Generated modules receive an optional versioned host-service
binding before property assignment and lifecycle callbacks. No Filament objects,
editor objects, or engine-loop methods cross that interface. Existing V1 modules
without the new binding continue to load.

## Compatibility and safety

Mesh serialization version 3 adds MaterialAsset; versions 1 and 2 load with no
override. Existing material slots are preserved. Legacy internal `.filamat`
imports still load for backwards compatibility, but public creation/picker
workflows never require users to manage these files.

The renderer validates material domain, vertex requirements and reflected GPU
parameter types before assignment. Invalid or unavailable overrides preserve the
imported/default material. On shader reload, old material packages remain alive
until all instances are destroyed; debug shading restores the correct material
before replacing instances. Runtime mutations are intended for the main gameplay
thread, like scene component mutations.
