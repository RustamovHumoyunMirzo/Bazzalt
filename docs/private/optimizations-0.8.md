# 0.8.0 performance work

The editor and players share the optimized core. No frame-rate cap, physics tick rate, rendering resolution, shadow quality or material update cadence was reduced. LOD is opt-in. Public mutable components remain supported: no persistent dirty cache assumes callers use a setter.

## Reproducing the CPU measurements

Build `BazzaltPerformanceBench` explicitly in Release; it is excluded from ordinary builds and is not a timing-gated CTest. Run the executable repeatedly after one warm-up. It measures 400,000 affine constructions, 4,000 world-matrix queries at depth 100, and 100 synchronizations of 200 static box colliders. Compiler configuration and machine load must match when comparing revisions.

Local Windows x64 Release samples (milliseconds):

| Workload | 0.7.0 samples | 0.8.0 samples | Median reduction |
| --- | --- | --- | --- |
| Affine 400k | 45.282, 45.877, 46.691 | 8.112, 8.021, 8.519 | 45.877 → 8.112 (5.66×) |
| Hierarchy | 187.492, 198.161, 198.413 | 71.838, 70.952, 69.987 | 198.161 → 70.952 (2.79×) |
| Physics | 173.498, 151.141, 176.788 | 47.411, 50.500, 47.240 | 173.498 → 47.411 (3.66×) |

The checksum was 20161996 in every run. These are CPU workload timings, not a claim of equivalent game FPS gains. GPU-bound projects may see a different benefit. Native regression tests also compare affine results to the original multiplication formula, including rotations and reflected/nonuniform scales.

## Implementation boundaries

- Math constructs affine columns directly and preserves the old path for nonfinite input. World matrices remain on-demand; direct Transform/Hierarchy edits are not hidden by a persistent cache. Cyclic transform ancestry throws instead of looping forever.
- Physics root poses are cached only within one Synchronize call. Key construction preserves the existing field byte order/separators while avoiding per-field temporary strings. Shape/config changes still synchronize normally.
- Primitive prototypes use exact topology parameter keys, shared immutable CPU buffers, and a 64-entry/32 MiB payload LRU. Color/material/render flags are not geometry keys. BufferDescriptor callbacks retain shared prototype ownership until upload completion. BVHs are generated once per prototype, and primitive picking retains its original determinant tolerance. Model traversal uses a fixed stack bounded above the depth of its balanced 32-bit BVH; corrupted excessive depth throws.
- Renderer instances compare transform arrays, render flags and primitive colors before uploading. Light resources compare sanitized parameters and world poses. Material values and texture feeds are still updated: do not throttle animated shaders, hot property edits or render-target textures to obtain a benchmark gain.
- Play inspector writes are elided only when incoming values are unchanged; editing/failure refreshes still restore authoritative data. Signal blockers are created only for actual updates.

## LOD ownership and lifetime

Public data is in Components/LOD.h; serialization is strict component version 1. Levels own child subtree UUID references. The renderer validates ownership and cross-level overlaps, then hides geometry per camera without changing entity/component enabled states or simulation. Invalid groups warn once until repaired and leave geometry visible.

LODViewState holds per-view hysteresis. Keep it for the camera/view lifetime, including skipped GPU frames; prune it when a viewport/camera is destroyed, when the scene changes, or on renderer shutdown. Removing history after a few frame attempts introduces boundary flicker when Filament skips frames.

Native asset visibility uses an exception-safe render scope that restores exactly the entities suspended for that view. Editor masks run inside the same scope; Graphics source/owner filtering handles deformed/custom draws. Each capture and presentation camera selects independently. No GPU asset reload occurs on a level switch. Editor picking and selection boxes exclude hidden representation geometry, while hierarchy authoring and logical/physics queries retain their existing roles.

Editor cloning defers LOD configuration until children exist, remaps subtree references, and restores forced levels after the new level count. Dropdown labels use localization formatting arguments. The level field uses the ordinary theme and entity MIME drag/drop; it never opens an OS file picker.

## Checks

BazzaltLODTests covers selection/cull hysteresis, orthographic behavior, forced/disabled levels, ownership validation, serialization, geometry/BVH equivalence, affine equivalence and stable-upload elision with live changes. Optional `--gpu` verifies actual near/far capture colors on Vulkan, forced culling and a boundary hysteresis hold. Editor tests cover controls, cloning, reference remapping, rejected edits and live inspector changes. Existing native, editor, launcher and compiler checks remain enabled.
