# Physics runtime and Jolt ownership

Physics implementation lives in `src/Physics/JoltWorld.cpp` and
`PhysicsGeometry.cpp`, built as `bazzalt_physics.dll` / `libbazzalt_physics.so`.
Jolt links statically only into that module. Core contains engine-owned data,
serialization, a public facade and a sibling-module loader, never Jolt headers
or solver code. The module imports core one way; core dispatches through
`PhysicsModuleApi` rather than importing the module's link library.

## Build and distribution

`scripts/dependencies/jolt.json` is the single source pin (5.3.0 at
0373ec0dd762e4bc2f6acdb08371ee84fa23c6db). get_jolt.py and its shell wrappers
fetch source and reject incorrect revisions/dirty trees. Dependency builds
belong on GitHub Actions. The Jolt dependency workflow checks Windows/Linux,
x64/x86 and publishes static library artifacts. Local engine work can use the
matching CI Release artifact through BAZZALT_JOLT_LIBRARY; do not build large
third-party dependencies on the developer PC.

cmake/Jolt.cmake scopes broad upstream options, uses static PIC, single
precision, 16-bit object layers and the existing SSE2 CPU baseline. It disables
AVX/SSE4/FMA options, LTO, upstream applications, object streams, profiler and
debug renderer. MSVC uses the DLL CRT. Linux x64 uses Clang/libc++; x86 uses
Clang/libstdc++. An imported library must match all these choices, architecture
and build configuration. Jolt types/includes/libs must never be exposed through
ScriptSDK or native game compilation. Only the sibling physics runtime ships.

Core loads the module relative to its own binary, with a version and table-size
check, not from the project/current directory. Module code remains loaded for
process lifetime, as callbacks/serializers may still refer to it. One process
service owns Jolt allocator/factory/type registration. Each scene world owns
its physics system, bounded job pool, temp allocator, body/shape maps and event
queues. The 16 MiB scratch arena uses Jolt's malloc fallback rather than aborting
when a step needs more temporary memory. Scene destruction and Clear release the world before registry storage.

Release packaging, standalone RuntimeTemplates, compiler validation and Hub
manifests include physics_abi=2. Old Hub payloads without that declaration remain
installable; new payloads must provide the matching physics library. Physics is
native game code and introduces no Python dependency in built games. Jolt's MIT
notice is included by the common third-party collector.

## Public data and serialization

Public Rigidbody/ShapeCollider/MeshCollider data has no runtime handles.
PhysicsFields.h is the field schema shared by serializers, editor bridge and
Lua bindings; update it when adding a field. Component serializers are version
1, and the scene Physics mapping has its own Version=1. Old scenes without the
mapping use defaults. Nullable Rigidbody.Gravity encodes as null; it is not a
fake zero vector. Validate physics settings before clearing a scene on load.
Unknown component descriptors keep the existing unresolved-component policy.

Body IDs are private generational Jolt handles. UUIDs identify body owners and
actual collider entities. A collider attaches to its nearest enabled rigidbody;
otherwise it is static. Nested rigidbodies are independent. Disabled parent
rigidbodies suppress attached colliders, without suppressing child bodies.
Collider-less bodies use EmptyShape and still simulate. Compound subshape user
data maps to collider UUIDs/material/layer/group/trigger properties.

## Synchronization, transforms and rendering

The main thread synchronizes authored edits at fixed-step/explicit boundaries, with structural changes observed through EnTT signals. Forces queue until the next step, so controls do not rescan/rebuild the entire scene per script. Queries read current physics poses while running and synchronize editing previews/topology. Signatures track component values, relative collider
poses, primitive dimensions, asset/node identity and geometry cache timestamps.
Quantize derived relative pose signatures to avoid rebuilding a body from
floating-point noise after parent movement. Shape data caches are bounded.
Rebuilds preserve runtime velocities unless the authored velocity was changed.

Offsets/rotations/scales compose with the entity world transform and are then
converted to a rigid body's local space. Use positive, non-singular scale.
Sphere scale uses the maximum axis; capsules/cylinders use the maximum X/Z
radial scale and Y height. Concave dynamic meshes and moving infinite planes
are rejected. Triangle meshes can be double-sided and sanitized. World-space
poses write back parent-before-child through SetWorldTransform; never overwrite
local transforms with world values. Center-of-mass offsets follow body scale.

The existing Time accumulator is the only scheduler. After native/Lua/scene
fixed callbacks, automatic physics advances once. AutoSimulation=false skips
solver work; public Simulate permits a manual fixed step only in a running
world. Invalid delta and recursive stepping are rejected. Global before/after
step notifications wrap actual automatic and manual steps, not idle frames.

Dynamic pose writeback is authoritative. RenderSystems uses the private
PhysicsRenderMatrix hook to interpolate previous/current poses by accumulator
alpha, including children of physical parents. It must not change Scene query
matrices or saved transforms. Teleport clears interpolation history. Selected
collider guides come from cooked shape triangles, not guessed render bounds;
they go through existing Scene-only guide rendering and respect Gizmos visibility.

## Queries and collision events

Vec3-origin Scene.Raycast/RaycastAll are physical; SceneRay overloads remain
visual geometry queries. ShapeCast/All and OverlapShape are on Scene too. The
PhysicsScene facade adds shape conveniences and body controls. Public facades
hold a weak scene lifetime token, so use after unload throws instead of touching
freed scene memory. Query results retain ordinary scene-scoped Entity values;
store UUIDs across scene lifetime boundaries.

Queries read the current world during Play (explicit Synchronize flushes raw transform edits) and automatically synchronize topology/edit-mode previews. They normalize direction, honor layer/trigger/backface/exclusion
filters, bound output, keep nearest hits per collider and sort cast results.
ShapeFilter inspects leaf user data only: compound subshape IDs are incomplete
while traversing, so never call GetSubShapeUserData on an intermediate compound
inside the filter. Returned body/subshape IDs are resolved only after a valid
result. The physics query path never calls scene/render picking fallbacks.

Broadphase distinguishes static/moving objects and uses ordinary single-layer
pair masks; mixed-layer compounds get exact per-leaf narrowphase filtering.
Same nonzero collision groups suppress contacts. Layer matrix edits invalidate
cached contacts. Per-collider friction/restitution/trigger state is applied in
the contact listener; mixed trigger/solid compounds keep their individual roles.

Worker listeners copy bounded contact data under a mutex. They never touch
EnTT, Lua or user callbacks. Active manifold counts aggregate Enter/Stay/Exit by
collider pair; up to 64 contact points are retained. Main-thread dispatch runs
after pose writeback and validates entity presence. UUIDs survive Exit when an
entity has gone. Subscription IDs are process-unique, preventing stale IDs from
removing a new scene world's listener. Dispatch snapshots subscription IDs;
removed listeners are skipped and additions wait for later events.

Subscriptions retain native module leases and script/subsystem owner scopes.
Disconnect owners before destroying a behavior/subsystem/VM; Lua protected
callbacks have instruction budgets. Native and Lua behavior callbacks are routed
to matching body/collider entities and respect script enablement. Do not hold a
Jolt body lock while invoking user code. Scene replacement may invalidate the
world during a callback; retained shared world state and its Dead flag prevent
subsequent dispatch into the old registry.

## Application/subsystem lifecycle

Persisted legacy levels 0..4 remain Boot, Window, Initialization, Content, Ready.
IDs 5..13 add Before/Input/AfterInput, Before/Audio/AfterAudio and
Before/Physics/AfterPhysics. GetLevelOrder supplies real ordering; do not compare
numeric IDs for permission or scheduling. UI groups use the same explicit order.
Input/Audio/Physics readiness guards are independent. Loading a higher stage may
listen to equal/later events, but subscriptions to earlier stages fail.

A player can create its native window and preload scene data before Play.
Content/post-window notifications are deferred while runtime services are not
ready; CompleteInitialization drains them after audio/physics initialization,
then reaches Ready. This avoids loading Physics-level subsystems prematurely.
Scene geometry loading alone does not start simulation. Input initialization
signals surround the real SDL setup; editor Play notifies availability of its
already initialized input service. Audio/physics initialization surrounds
service/world startup. Stop destroys script callbacks before releasing worlds
and audio; application shutdown disconnects subsystem callbacks.

## Tests and scope

BazzaltPhysicsTests exercises real CPU simulation/contact points, impulses,
gravity overrides, layers, triggers, physical queries/casts/overlaps, mesh
colliders, disabled bodies, serialization, destruction and facade lifetime.
Editor tests cover native field roundtrips, nullable gravity and collision-layer
editing. Keep standalone cooked-game, native C++/Lua and Windows/Linux x86/x64
checks when changing the module. Preserve dependency-only registration/teardown
checks as the fast build/ABI gate.

Do not claim vehicles, character controllers or cross-platform deterministic networking. Native callbacks
remain trusted C++ code. Jolt objects/streams/BodyID never become scene formats
or gameplay ABI. Add public API changes to C++, Lua, generated references and
What's New together.

## Mesh authoring and hot paths

An empty MeshAsset resolves attached primitive/mesh data, model-node metadata,
or mesh descendants under a group. Traversal stops at nested Rigidbodies.
Relative transforms are composed from local hierarchy matrices, avoiding world
translation cancellation as a body moves. Explicit asset overrides retain their
source-node semantics when the asset matches the attached mesh.

Per-synchronization revision maps share filesystem checks. Binary comparison
keys avoid per-field locale streams and canonicalize signed zero. CPU model
geometry (128 MiB), flattened mesh inputs (64 MiB), cooked leaf snapshots
(64 MiB), and local guide vertices (32 MiB) have bounded caches. Cooked snapshots
use Jolt SaveBinaryState/RestoreBinaryState and explicit material references;
they are process-local trusted data, never loaded from project files. Each
restored collider receives its own user data for queries and contacts. Hull
point deduplication removes exact duplicates only. Collision mesh fidelity and
solver steps are not reduced.

Editor overlays synchronize once for a selection batch. Guide tessellation is
cached in local space; world points are recomputed when the pose changes.
Dense diagnostic edges are sampled within a drawing budget. The renderer
compares guide batches and skips identical GPU uploads. These limits affect
visual debugging only. Selected physics guides bypass scene depth culling.

Bshader time still uses Filament's material clock. Each frame supplies a shared
steady-clock timestamp and an epoch derived from scaled Bazzalt Time. All
captures/viewports therefore see the same paused/scaled shader time. Outside
Play the editor uses an independent preview epoch.

### Local performance check

One Windows x64 Release run on an isolated copy of the 7.46 MB car model:

| Workload | Before | After |
| --- | ---: | ---: |
| Initial collider preparation | 266.4 ms | 207.2 ms |
| 50 stationary guide requests after synchronization | 8.34 ms | 0.22 ms |
| 10 friction-only edits and synchronizations | 1430.2 ms | 1.02 ms |

These are focused CPU benchmarks, not an overall editor FPS claim. Cold cooking
still performs real mesh decoding/hull or BVH construction. Private guide callers
synchronize once per selection batch. Regression fixtures verify node/subtree
scope, exclusion of unrelated branches and nested bodies, transformed groups,
geometry invalidation, cached queries, guide bounds, paused/scaled material time,
and unchanged GPU upload suppression.


## Joints and soft bodies

Physics module ABI 2 adds joint control, soft vertex control/readback and render submission. Update packaging metadata and reject mismatched siblings. Public data remains Jolt-free. PhysicsExtras.inl implements the solver adapters; PhysicsExtras.h owns native references in World. DetachJoints must run before every rigid body destruction/recreation. Constraints retain stable component keys and break state while rebinding native bodies. Missing references are inactive. Collision suppression is immutable during Update. Break force/torque use solver impulse divided by step duration.

Cloth reads the attached primitive or mesh/model subtree through PhysicsGeometry.ReadSoftMesh; Rope generates its tube. Both create optimized Jolt SoftBodySharedSettings, including faces, stretch/shear and dihedral bend constraints. Pins are inverse-mass zero particles; runtime edits modify inverse mass and velocity only. No position teleport is used for pinning. Soft bodies enable Jolt mUpdatePosition and write runtime world poses back parent-before-child alongside rigid bodies. Creation vertices are centered locally and the initial bounds-center offset is retained, preserving the source entity pivot. Cache the reconstructed scene pose after writeback to avoid treating decomposition roundoff as an authored teleport. Particle positions remain world-space. Authored pose edits move the current deformed body; the editor synchronizes them immediately even when paused. Play snapshot restoration restores the original scene on Stop. Soft-body boundaries also stop rigid interpolation ancestor lookup. Do not combine another physics body/collider on the same entity. Topology, mass, scale and compliance changes recreate the soft body. Wind, damping, gravity factor, solver iterations, contact material and render settings update in place. Geometry resolution is bounded to prevent accidental unbounded allocation.

GraphicsAccess.BeginFrame calls RenderPhysicsScene after the graphics frame begins. The physics module submits current world-space deformed geometry via GraphicsMesh; unchanged geometry retains its revision. Each frame recomputes area-weighted normals. Gameplay and editor use the same surface/material pipeline. Selected guides show the deforming triangles. Soft-body collision resolution uses Jolt; rigid contact subscriptions do not currently include soft-body contact manifolds. No self-collision, tearing, runtime rigid attachment/skinning or cross-platform deterministic simulation is promised.

Add field definitions once in PhysicsFields.h, then extend the serialization registry, bridge dispatch, Lua registration, inspector types and API inventory. Never expose native constraint/body handles. Tests must cover stepping, pins, body recreation, broken constraints, component disable/removal, serialization and teardown.


### Editor soft-body failure isolation

The bridge validates incompatible component additions before mutating the registry. Preview synchronization catches soft-body authoring errors per entity, reports each distinct error once, removes stale native soft bodies, and keeps valid render submissions alive. Explicit Synchronize and game simulation remain strict. Hierarchy bounds reads catch failed physics readback rather than propagating it through SceneEntities and clearing the hierarchy. Regression tests initialize the editor renderer, exercise rejection in both component orders, preview legacy invalid data alongside valid cloth, and repair it without restarting.


### Source mesh deformation and stable rendering

Cloth consumes actual source triangles. Coincident solver particles are welded with a scale-relative tolerance, including UV seams and primitive poles; render vertices retain material slots, UVs and hard-normal groups. Primitive Plane resolution comes from its Segments/Rings. Source geometry is bounded to 16,384 welded particles; oversized input reports a diagnostic rather than simplifying geometry. Empty entities do not implicitly generate a cloth plane. Width/Height/SegmentsX/SegmentsY remain serialized only for compatibility. Nested rigid/soft-body ownership boundaries are excluded; unowned child rest-pose colliders covered by cloth are suppressed while Cloth is enabled.

Cloth surfaces submit source owner/node/material-slot metadata through the private Graphics bridge. RenderAssets suspends original surfaces for the render frame, and restores them afterward; enabled/disabled components and source visibility are honored. Live source material instances are borrowed rather than duplicated every tick. RenderAssets must notify GraphicsRenderer before destroying a source mesh or material override, so borrowers detach first. Renderer shutdown clears this callback before asset teardown. Deformed geometry participates in the selection mask; rest-pose sources are excluded.

GraphicsRenderer reuses vertex/index buffers and renderable components for matching topology/counts across revisions. Frame snapshots with two revisions of one mesh remain independent. Geometry uploads recompute UV-based tangent frames and bounds; source materials, including normal maps, use those attributes. Regression tests assert source replacement, inherited materials, stable GPU allocation/build counters over 40 changing frames, safe source teardown, source-node scoping and cube-vs-plane geometry. Public rendering and physics APIs never expose Filament/Jolt handles.


### Cloth material, pressure and schema v2

Cloth defaults follow the compliant soft-shell approach in Jolt's sample creators: stretch/shear compliance 1e-4, optional bending disabled, and bend compliance 1e-3 when bending is enabled. Never infer softness from a translation-only gravity test. Regression tests require actual draping over a sphere and impact deformation, and compare unpressurized versus pressurized shells. The inspector needs eight decimal places; rounding a compliance value to zero changes its physical meaning.

ClothBendMode maps to native EBendType None/Distance/Dihedral. Pressure maps directly to SoftBodyCreationSettings.mPressure and live SoftBodyMotionProperties.SetPressure. It is Jolt's nRT term. Cache a closed-surface check from welded faces: every edge has two oppositely directed uses and each connected component has positive nonzero signed volume. Reject positive pressure for open/inward/inconsistent topology before creation and on live changes. CollisionRadius maps to SoftBodySharedSettings.mVertexRadius in the pinned Jolt 5.3 API. No native types enter the public ABI.

SubdivisionLevel requests at most four adaptive midpoint-refinement passes before welding. Apply the same pass to every material part to prevent seam T-junctions; stop at the render-vertex/triangle budget before allocation. Preserve UVs, normals, colors and source owner/node/slot metadata. Mass is distributed over welded particles. Subdivision, bend mode and collision radius are structural; pressure is updated in place. Existing renderer streaming and material release rules still apply.

Cloth serialization is component version 2. Version 1 supplies new controls from defaults. Its exact former default tuple (zero stretch/shear and 1e-4 dihedral compliance) migrates to the corrected fabric defaults. Other authored coefficients are retained and retain Dihedral bending. Version 2 requires all fields. Do not silently drop fields or reinterpret malformed data. The scripting SDK digest hashes all public headers, so native project modules recompile for the extended component layout.
