# Runtime materials (C++ and Lua)

Runtime material APIs use public headers only. They never expose Filament engines,
GPU handles, render-loop operations, or a runtime shader compiler. Call them on
the scene/game thread while a runtime host is active.

## Create and define

```cpp
#include <Bazzalt/MaterialBuilder.h>
#include <Bazzalt/Components/PrimitiveObject.h>

auto surface = Bazzalt::MaterialBuilder()
    .SetColor("baseColor", {0.2f, 0.5f, 0.9f, 1.0f})
    .SetFloat("roughness", 0.35f)
    .SetFloat("metallic", 0.1f)
    .Build();
surface.ApplyTo(entity);
surface.SetFloat("roughness", 0.8f); // Live renderer update.
```

`Shader::Builtin(ShaderPreset)` and `Material::Create(ShaderPreset)` support
`StandardLit`, `Unlit`, `StandardLitTransparent`, and `UnlitTransparent`.
All presets define `baseColor` (Vec4, default white) and `emissive` (Vec3,
default zero). Lit presets additionally define `roughness` (0.5), `metallic`
(0), and `reflectance` (0.5). Color values are linear. Transparent presets use
baseColor alpha; opaque presets do not become transparent by changing alpha.

For custom layouts, import a shader asset and use `Shader::Load(shaderUUID)`.
Shader programs and their parameter layouts are compiled during import/build;
runtime builders define parameter values, not arbitrary GLSL programs.
Typed setters support floats, vectors, colors, matrices, integers, booleans and
texture asset UUIDs. Unknown names, incompatible types and non-finite values
are rejected. `Build()` cleans up its transient material if validation fails.
Repeated builder setters apply in order; the last value wins. `Clear()` removes
queued values and render state, retaining the shader.

## Lifetime and sharing

`Material::Load(assetUUID)` references a shared imported material. Runtime edits
affect all users of that handle, but never write the asset file. `Instantiate()`
clones values into an independent transient material; `Create()` also creates a
transient material. Keep handles as long as needed and call `Destroy()` when no
longer used. It returns false for imported materials. Destroying a transient
invalidates every copy of its handle; assigned renderables fall back to their
original material on their next update. Runtime materials expire on runtime
reset/Stop and cannot be persisted as assets merely by saving their UUIDs.

## Change, inspect and reset

- `GetParameters()`, `HasParameter()`, and typed `Get*()` inspect the shader layout
  and effective values. `HasOverride()` distinguishes a runtime override.
- `ResetParameter(name)` restores the authored/shader default value;
  `ResetProperties()` removes all parameter overrides.
- `SetShader(shader, preserveProperties=true)` switches the actual renderer
  shader. Only values with matching names and types are retained.
- `CopyPropertiesFrom(source, copyRenderState=true)` copies compatible values
  without changing the target shader. Source reimports may restore an imported
  material's authored shader; these methods do not modify source assets.

## Render state

`MaterialRenderState` provides `DoubleSided`, `Culling`, `ColorWrite`, `DepthTest`,
`DepthWrite`, and `DepthFunction`. `SetRenderState(state)` enables the override.
`GetRenderState().Override` reports whether one is active. `ResetRenderState()`
restores the compiled shader defaults, including its depth comparison function.
Use `MaterialCulling` and `MaterialDepthFunction` enums, not backend constants.
Built-in presets compile double-sided capability and default to double-sided
surfaces. Imported shaders must declare `doubleSided: true` for double-sided
lighting overrides; culling changes alone do not add that shader capability.
Blending and shader features are compiled properties: switch shader presets or
import another shader to change them. Disabling depth writing/testing can cause
overlap artifacts and is an explicit advanced setting.

## Assignments and slots

`ApplyTo(entity, slot=Material::AllSlots, includeChildren=false)` assigns existing
Mesh/PrimitiveObject components and returns the number of affected entities.
Children are untouched unless explicitly requested. Primitive objects have only
slot zero; invalid slots are rejected before assignments occur.

`Mesh::SetMaterial(material)` overrides every slot; `SetMaterial(slot, material)`
sets a zero-based slot. `GetMaterial(slot)`, `GetMaterialCount()`, and
`ClearMaterial(slot)` manage individual slots. `ClearMaterial()` removes all
overrides. Counts reflect known slots, which can be unavailable before model
loading. Original glTF embedded materials are renderer-owned, not automatically
public material assets; a slot without an asset override returns an invalid
handle. PrimitiveObject has `SetMaterial`, `GetMaterial`, and `ClearMaterial`.

## Lua parity

```lua
local B = Bazzalt
local surface = B.MaterialBuilder.new()
    :SetColor("baseColor", B.Vec4.new(0.2, 0.5, 0.9, 1))
    :SetFloat("roughness", 0.35)
    :Build()
surface:ApplyTo(self.Entity) -- Direct scene assignment.
surface:ApplyTo(self.Entity, B.Material.AllSlots, true) -- Explicit descendants.
surface:SetShader(B.Shader.Builtin(B.ShaderPreset.Unlit), true)
```

Lua exposes the same lifecycle, typed properties, render state, builder and
component helpers. `Material.AllSlots` is -1 in Lua; actual slots remain zero-based.
Lua component getters return snapshots: commit edited components with
`entity:SetComponent("Mesh", mesh)` or use `ApplyTo()` for direct assignment.
Destroy owned materials in `OnDestroy`; do not destroy a material still needed
by another script. Imported material assets cannot be destroyed with this API.

## Extending the implementation

Public declarations live in `include/Bazzalt`, definitions in private
`Runtime/MaterialLibrary`, and GPU application in private `Rendering/RenderAssets`.
New operations must also be registered in `Script/LuaAssets` (component helpers
in `LuaComponents`) and exercised in MaterialTests and LuaRuntimeTests. New
built-in shaders are embedded by CMake, not loaded from editor directories.
