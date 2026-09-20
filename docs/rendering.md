# Rendering

## Boundary

Rendering is described through ECS components. Public code never receives a
Filament engine, renderer, view, entity, texture, material, buffer, or destroy
function. Private systems translate scene data into Filament state.

## Camera

```cpp
#include <Bazzalt/Components/Camera.h>

auto cameraEntity = scene.CreateEntity("Main Camera");
auto& camera = cameraEntity.AddComponent<Bazzalt::Camera>();
camera.Projection = Bazzalt::CameraProjection::Perspective;
camera.VerticalFieldOfView = Bazzalt::ToRadians(60.0f);
camera.NearPlane = 0.1f;
camera.FarPlane = 2000.0f;
camera.Priority = 10;
camera.PostProcessing.Bloom = true;
```

The highest-priority active camera becomes active. Cameras support perspective
or orthographic projection, aspect ratio, clear color, and renderer-neutral
post-processing settings. Current synchronization applies projection, FXAA/TAA,
bloom, ambient occlusion, and enablement. Clear color, tone mapping, and
exposure await full presentation integration.

The entity transform defines position/orientation. Negative Z is forward and
positive Y is up.

## Lights

```cpp
#include <Bazzalt/Components/Light.h>

auto sunEntity = scene.CreateEntity("Sun");
auto& sun = sunEntity.AddComponent<Bazzalt::Light>();
sun.Type = Bazzalt::LightType::Sun;
sun.Color = {1.0f, 0.96f, 0.9f};
sun.Intensity = 100000.0f;
```

Types are `Directional`, `Sun`, `Point`, and `Spot`. Intensity is lux for
directional/sun and lumens for point/spot. Point/spot use `Range`; spot also
uses inner/outer cone radians. Transform supplies position and negative-Z
direction.

## Meshes and materials

```cpp
#include <Bazzalt/Components/Mesh.h>

auto model = scene.CreateEntity("Robot");
auto& mesh = model.AddComponent<Bazzalt::Mesh>();
mesh.MeshAsset = robotModelUuid;
mesh.Materials = { bodyMaterialUuid, glassMaterialUuid };
mesh.LayerMask = 0xff;
mesh.Visible = true;
mesh.CastShadows = true;
mesh.ReceiveShadows = true;
```

`MeshAsset` accepts glTF/GLB or filamesh-family asset UUIDs. `Materials` holds
compiled `.filamat` UUIDs for filamesh overrides. Index zero is
`DefaultMaterial`; later entries are `Material1`, `Material2`, etc. glTF owns
its authored PBR materials/textures, so overrides currently apply to filamesh.

Changing the mesh/material UUIDs recreates the private render instance.
Transform, visibility, layers, and shadows synchronize continuously.

## glTF

gltfio parses GLTF/GLB, creates Filament renderables, loads external/embedded
resources, and creates glTF material variants. PNG/JPEG payloads use Filament's
stb_image-backed provider. Parse data is released after upload. Each ECS model
owns an asset instance that is removed from the render scene before destruction.

## Filamesh and shaders

OBJ/FBX authoring files are converted by `filamesh` with interleaving and
compression. filameshio loads the resulting renderable, vertex buffer, and
index buffer.

Filament `.mat` sources compile with `matc` for desktop and all graphics APIs.
Runtime builds private `filament::Material` objects from `.filamat` and creates
material instances for renderables.

## Current scope

This foundation covers resource ownership and ECS synchronization. Window and
swap-chain creation, editor viewport presentation, game presentation, material
instance authoring, environment lighting, and a complete frame graph are later
layers. Public components remain backend-neutral for that evolution.
