"""Reviewed explanations for the public API. Missing descriptions fail the docs build."""
import re
TYPE_NOTES = {
 'AssetState':'The import state of a registered asset. Ready means runtime data is available; a UUID alone does not prove an import succeeded.',
 'AssetInfo':'A read-only description of an asset in the active project database. Paths describe source, metadata and generated cache files; this value does not own them.',
 'AssetManager':'Read-only access to the active project asset database. Call static methods from hosted gameplay; there is no public project-open/import loop.',
 'Component':'Base data state for components. Derive a custom data component from this type. Ordinary components can be enabled without being removed.',
 'ComponentInheritanceMode':'Declares how a component is resolved through a parent hierarchy. Adding a component to a parent never automatically copies it onto children.',
 'ComponentInheritance':'Template trait specialized by a game or engine extension to opt a component into ancestor lookup. The default Mode is None.',
 'ComponentInheritance<Transform>':'Transform specialization: the world transform is composed from local transforms, not borrowed as a nearest-ancestor data component.',
 'CameraProjection':'How 3D points project into a camera image: perspective decreases apparent size with distance; orthographic does not.',
 'CameraAspectMode':'Chooses whether the projection aspect ratio follows the viewport dimensions or uses a fixed ratio.',
 'AntiAliasing':'Camera anti-aliasing mode. Anti-aliasing reduces stair-stepped edges; temporal modes also use previous frames.',
 'ToneMapping':'Converts rendered high-dynamic-range color into a displayable range. This is separate from scene illumination.',
 'DepthOfFieldQuality':'Selects the camera depth-of-field quality/performance tier.',
 'CameraViewport':'A normalized rectangle on a presentation surface. The origin is bottom-left. A half-width viewport has Width 0.5, not a width in pixels.',
 'CameraPostProcessing::DepthOfFieldSettings':'Camera lens/blur settings. Depth of field blurs geometry away from the focus distance; enable both post-processing and this effect. Lua calls this type DepthOfFieldSettings.',
 'CameraPostProcessing':'Effects on this camera image, independent of other cameras. Nested values belong to the Camera component and must be committed with it in Lua.',
 'Camera':'A game camera component. Requires an entity Transform. Both component Enabled and camera Active must permit rendering. The editor navigation camera is separate.',
 'GaussianBlur':'An image-wide Gaussian blur component on a camera entity. This is not depth of field: it blurs the whole image rather than choosing a focus distance.',
 'Hierarchy':'Scene-managed parent and child UUID data. Inspect this component, but mutate relationships using SetParent/AddChild/RemoveParent to preserve invariants.',
 'Identity':'Scene-managed entity UUID. Do not replace it or use a renamed display label as a stable identity.',
 'LightType':'Light geometry/illumination mode. Type determines which intensity units and guide settings apply.',
 'Light':'Illumination emitted from the owning entity. Transform determines its position/direction. Range and cone fields only apply to the corresponding light types.',
 'Mesh':'A renderable imported model/mesh reference with optional material overrides. Zero overrides retain the original imported material.',
 'ModelInstance':'Identifies the imported asset at the parent of an instantiated model hierarchy. The child nodes are entities, not separate asset files.',
 'ModelNode':'Import identity of a model child. StablePath allows the model structure to be matched across import changes. A grouping node need not contain a mesh.',
 'Name':'Human-readable entity label. Names may repeat and change; use UUID for stable identity.',
 'PrimitiveShape':'Available built-in procedural shapes; no model asset needs to be imported for these.',
 'PrimitiveObject':'Procedural renderable geometry. Shape decides which dimension fields are used. Entity Transform still controls placement, orientation and overall scaling.',
 'SceneQueryShape':'Geometry used for explicit scene query bounds. These bounds are not physics colliders.',
 'SceneQueryBounds':'Optional local selection/query geometry for an entity. Center and Extents/Radius are in local space and are transformed with the entity.',
 'ScriptPropertyValue':'One serialized script Inspector property. Its strings describe authored data, not a pointer to a running behavior instance.',
 'ScriptAttachment':'One authored behavior attachment. Source identifies its asset; the runtime privately loads executable code or Lua bytecode.',
 'ScriptComponents':'The authored list of script attachments on an entity. This is storage metadata, not an engine-loop or arbitrary dynamic script execution API.',
 'Transform':'Local position, quaternion rotation and scale. GetWorldTransform/SetWorldTransform on Entity convert between this local data and composed world placement.',
 'Vignette':'Camera image edge darkening/color effect. This is post-processing, not a light or object material.',
 'Entity':'A non-owning scene object handle. Default construction is invalid. Do not retain or call handles after their scene has been destroyed.',
 'EntityReference':'A UUID-based non-owning reference that resolves against the active scene each time, rather than retaining registry storage.',
 'KeyCode':'Physical keyboard identifiers, independent of operating-system virtual-key values. Use these constants with Input rather than hard-coded platform key numbers.',
 'MouseButton':'Mouse-button identifiers used by Input.',
 'InputAxis':'Named keyboard or mouse axes. Horizontal/Vertical are keyboard axes, not analog controller bindings.',
 'Input':'Static, read-only main-thread gameplay input. Inactive focus returns neutral values. It does not pump OS events or bind editor navigation controls.',
 'MaterialCulling':'Which triangle faces are culled (not drawn). FrontAndBack intentionally removes all faces from that pass.',
 'MaterialDepthFunction':'Comparison between a fragment depth and existing depth. Changing it does not create alpha blending or compile another shader.',
 'MaterialRenderState':'Optional abstract render-state override. SetRenderState enables it; ResetRenderState restores compiled shader defaults. No Filament handle crosses this API.',
 'Material':'A handle to shared imported or owned transient material state. Runtime property edits affect rendering without writing the source asset.',
 'MaterialBuilder':'Queues typed values against a precompiled shader, then creates a validated transient Material. It does not compile shader source or introduce undeclared uniforms.',
 'Vec2':'Two floating-point coordinates. Useful for screen positions and 2D directions. Arithmetic returns values; zero normalization returns zero.',
 'Vec3':'Three floating-point coordinates. Used for positions, directions, sizes and RGB colors. Directions are not automatically unit length.',
 'Vec4':'Four floating-point coordinates. Used for RGBA colors or homogeneous coordinates; W distinguishes a point from a direction in matrix calculations.',
 'Mat3':'A 3×3 row-major matrix operating on column vectors. Default construction is identity. Indexing is zero-based.',
 'Mat4':'A 4×4 row-major transformation matrix operating on column vectors. Translation occupies the last column; combined TRS applies scaling, rotation, then translation.',
 'Quaternion':'Rotation stored as X/Y/Z/W, not four Euler angles. Identity has W=1. Prefer normalized rotations and use radians with construction helpers.',
 'MathFunctions':'Free functions, constants and free arithmetic operators in namespace Bazzalt. Angles use radians unless a function explicitly converts degrees.',
 'ModelAssetNode':'One node in an imported model description. Node children and root entries are source/node indices; no scene entity is created until instantiation.',
 'ModelAsset':'A loaded model description containing node data. InstantiateModel converts it into an editable scene entity hierarchy.',
 'PostProcessParameterType':'The kind of value supplied to a compiled custom post-process shader parameter.',
 'PostProcessParameter':'One named custom post-process value. Use factory methods to populate the appropriate typed storage field.',
 'CustomPostProcessEffect':'One custom full-screen effect referring to a precompiled shader asset. Order controls its position in the camera stack.',
 'PostProcessingStack':'Ordered custom camera effects. C++ references into its vector can be invalidated by additions/removals; Lua getters return copied effect values.',
 'ProjectMetadata':'Project descriptor data. Gameplay does not get authority to open or save a project merely because this public data structure exists.',
 'ProjectSerializer':'Runtime-owned project persistence implementation. It has no public callable save/load API and is not construct-and-use gameplay tooling.',
 'Scene':'Entities, hierarchy, environment and geometry queries in one scene. The host advances systems; gameplay cannot call the private scene update loop.',
 'SceneEnvironmentMode':'Scene background source mode: an environment map or a material-based background.',
 'SceneEnvironment':'Scene-owned environment settings. Visible background and image-based lighting are independent. Zero asset IDs do not import or create a default sun.',
 'SceneManager':'Small static gameplay facade for active-scene lookup and deferred loading. It intentionally does not expose project loading, authoring saves or engine initialization.',
 'SceneRay':'World-space origin and direction for a scene geometry query.',
 'SceneQueryOptions':'Filters and distance limit shared by picking, ray and overlap queries. Queries operate on scene geometry, not a simulated physics world.',
 'SceneQueryHit':'Result of a scene ray/pick query. An empty Target means no hit; other fields describe the hit surface when valid.',
 'Behavior':'Attachable gameplay lifecycle base. The editor/runtime constructs instances, binds their entity, supplies property values and calls callbacks.',
 'ScriptRuntimeAccess':'Host/compiled-module bridge declared in a public header for ABI plumbing. Do not call its binding methods to take over the application loop.',
 'PropertyType':'Native script property category used by tooling and module metadata.',
 'ScriptModuleApi':'Versioned native module C-ABI callback record. Generated module entry points supply this; ordinary COMPONENT authors should not manually construct it.',
 'SerializedComponent':'A versioned component record with string properties. Unknown/plugin component data can be kept without pretending the component is executable.',
 'UnresolvedComponents':'Records for component types not available during loading. Resaving preserves them for forward/plugin compatibility.',
 'ComponentSerializationRegistry::Descriptor':'Serializer registration with type name, schema version and encode/decode callbacks. This is extension tooling rather than scene-saving permission.',
 'ComponentSerializationRegistry':'Registration and lookup of component codecs. A custom C++ struct needs a codec for persistence; its name alone is not serialization.',
 'SceneSerializer':'Host-owned scene serializer. Save/Load and construction are private. Public methods only inspect the registry/error through an authorized host-owned instance.',
 'ShaderPreset':'Precompiled built-in surface shader choices: lit/unlit, opaque/transparent. Transparent alpha needs a transparent preset.',
 'ShaderParameterType':'Declared shader parameter type. A setter must match this reflected type; Texture2D is an asset UUID reference.',
 'ShaderParameter':'A reflected parameter name/type pair, returned by shader/material inspection.',
 'Shader':'UUID-backed handle to a precompiled/imported shader definition. Load creates a handle; IsValid verifies the host can resolve its definition.',
 'System':'Scene-owned processor lifecycle. Derive and implement OnUpdate; do not call it as an application frame loop.',
 'ComponentSystem':'System helper template for processing component views. It exposes protected helpers for derived systems, not a concrete engine-owned update service.',
 'Time':'Main-thread gameplay clocks and time scaling. The runtime alone advances them. Time scale changes simulation delta, not editor frame presentation.',
 'UUID':'128-bit stable identifier. Zero represents the scene root but is not a valid nonzero asset identifier. A UUID does not own an entity or asset.'
}

COMMON = {
 'GetShader':'Returns this material\'s current shader handle, or an invalid shader if its material definition cannot resolve.',
 'IsEnabled':'Returns whether this component/system is enabled. Entity-level checks for required default components have their own restrictions.',
 'SetEnabled':'Sets the enable flag without removing stored values. The owning system decides how disabled components are skipped.',
 'Enabled':'Enable flag; false retains the data while ordinary processing skips it. Required default entity components cannot be disabled through Entity APIs.',
 'GetUUID':'Returns the stable UUID identifying this value/entity/scene. It is not a filename or display name.',
 'GetAssetUUID':'Returns the referenced material/shader UUID. It may be unresolved; call IsValid before assuming the definition is available.',
 'LengthSquared':'Returns the sum of squared coordinates, avoiding a square root. Use it for comparisons or testing a vector against zero.',
 'Length':'Returns Euclidean magnitude (square root of LengthSquared). This is not a direction or a normalized value.',
 'Normalized':'Returns a normalized copy and leaves the original unchanged. Near-zero vectors return zero; near-zero quaternions return identity.',
 'Normalize':'Normalizes this value in place. It changes the original rather than returning an independent normalized object.',
 'Dot':'Returns the scalar dot product. Unit direction vectors give the cosine of their angle; quaternions also use it for interpolation.',
 'Lerp':'Linear interpolation from the first value to the second by amount. Amount is not clamped: 0 selects from, 1 selects to, other values can extrapolate.',
 'Identity':'Returns an identity matrix/rotation that leaves positions or directions unchanged.',
 'Transposed':'Returns a copy with rows and columns exchanged; does not modify the input matrix.',
 'Inversed':'Returns an inverse. Singular matrices return an all-zero matrix; a near-zero quaternion returns identity. Use TryInverse when you need explicit failure.',
 'GetWorldMatrix':'Returns the composed parent-to-child world transformation matrix, not just the local Transform matrix.',
 'GetWorldTransform':'Returns a world-space Transform value. Edit the returned copy and call SetWorldTransform to commit it.',
 'SetWorldTransform':'Commits world placement by converting it into local Transform data under the parent. Returns false for a rejected/non-decomposable placement.',
 'SetParent':'Reparents the child/entity. worldPositionStays defaults true; false preserves local values. Returns false for invalid/cross-scene/cyclic links or failed world preservation.',
 'RemoveParent':'Reparents to the scene root, normally preserving world placement. Returns whether the operation succeeds.',
 'GetParent':'Returns the parent handle (root where applicable), or an invalid handle when there is no resolvable parent.',
 'GetChildren':'Returns direct children, not all descendants. Returned handles do not own those entities.',
 'HasParent':'Returns whether GetParent resolves a valid entity. The permanent scene root counts as a parent of ordinary top-level entities.',
 'IsAncestor':'Returns whether the first entity is an ancestor of the descendant in this scene; an entity is not its own ancestor.',
 'IsAncestorOf':'Returns whether this entity is above another in the hierarchy, without changing either relationship.',
 'CreateEntity':'Creates an entity with required default components and root parenting. The explicit UUID overload requires an unused nonzero ID; the name overload generates one.',
 'DestroyEntity':'Destroys the specified scene entity and descendants. Existing handles/references to their storage must no longer be used. The permanent root cannot be destroyed.',
 'FindEntityByName':'Returns the first entity with the display name, or an invalid handle when absent. Names are not guaranteed unique.',
 'GetEntity':'Resolves a UUID or transient registry ID in this scene. Returns an invalid Entity when it cannot resolve; registry IDs are not persistent asset IDs.',
 'GetRootEntity':'Returns the permanent root whose UUID is zero. It is not a camera or ordinary deletable object.',
 'GetEntityCount':'Returns the number of live registry entities, including the permanent root; this is not draw-call count.',
 'GetEnvironment':'Returns current scene environment data. In C++ it is a const reference; Lua receives a copy to commit with SetEnvironment.',
 'SetEnvironment':'Validates and commits a complete environment value. Returns false if its mode, intensity, rotation or clear color is invalid.',
 'InstantiateModel':'Creates a parent and child entity tree from ModelAsset or its UUID, preserving node transforms and mesh references. Returns the instance parent; this does not create new source assets.',
 'GetSystem':'Returns a registered system; lookup of an absent type/name fails instead of silently creating it.',
 'HasSystem':'Returns whether the specified system type/name is registered, regardless of its enable state.',
 'AddSystem':'Creates/registers a system and calls OnCreate. C++ returns an existing same-type system; Lua requires a unique name and table. Do not add during system updates.',
 'RemoveSystem':'Removes the registered system and calls OnDestroy. Returns false if absent; modification during system updates is rejected.',
 'OnCreate':'Host callback called after an instance is created and initial properties are applied. Initialize runtime state here; do not initialize the engine.',
 'OnUpdate':'Host callback for enabled frame updates. deltaTime is scaled seconds, not an FPS count. Multiply rates by it for time-based behavior.',
 'OnFixedUpdate':'Host callback for enabled fixed-step work. The supplied step is in seconds; the host owns scheduling.',
 'OnDestroy':'Host cleanup callback. Release owned transient resources; do not destroy shared assets or take over host shutdown.',
 'HasMesh':'Reports whether this node contains a mesh rather than only grouping/transform data.',
 'SetMaterial':'Assigns a material UUID override. The Mesh slot overload targets a zero-based slot (0–4095); the single-argument overload overrides all slots. PrimitiveObject has only one slot.',
 'GetMaterial':'Returns the assigned material handle. A missing override can be invalid even if the imported original surface is rendering normally.',
 'ClearMaterial':'Removes the material override, restoring the imported/built-in surface on an ensuing update. The slot overload clears only that zero-based slot.',
 'GetMaterialCount':'Returns known assigned/loaded material-slot count. This can be incomplete before a model is loaded and is not an asset count.',
 'FullScreen':'Returns normalized rectangle (0, 0, 1, 1), covering the whole presentation surface.',
 'LeftHalf':'Returns the left half rectangle (0, 0, 0.5, 1).', 'RightHalf':'Returns the right half rectangle (0.5, 0, 0.5, 1).',
 'TopHalf':'Returns the top half rectangle (0, 0.5, 1, 0.5).', 'BottomHalf':'Returns the bottom half rectangle (0, 0, 1, 0.5).',
 'Grid':'Returns a normalized cell for zero-based column/row in columns×rows. Invalid dimensions/indices return an empty viewport, not an exception.',
 'GetLastError':'Returns diagnostic text for the latest failed service operation. A previous successful request does not guarantee another operation cannot fail.',
 'GetParameters':'Returns the reflected parameter names and types. It does not return renderer handles or compile a shader.',
 'HasParameter':'Tests whether the current shader declares the parameter name. Use this before selecting a typed setter dynamically.',
 'Build':'Creates and returns an owned transient material, applying queued values. Invalid shader/name/type/state values throw; the failed temporary is destroyed.',
 'CopyPropertiesFrom':'Copies effective compatible name/type values from another valid material, optionally also its render state. It does not switch the target shader.',
 'SetShader':'Sets a precompiled shader. Compatible values can be preserved (default true). Invalid material or shader throws; this never invokes a shader compiler.',
 'SetRenderState':'Commits render-state overrides and enables Override. Invalid enum values or material handles throw; compiled blending/features remain part of the shader.',
 'ResetRenderState':'Removes the override so the next renderer update restores the shader package defaults, including depth comparison.',
 'GetRenderState':'Returns the current render-state value, including whether Override is active. Invalid material handles throw.',
 'ResetParameter':'Removes one runtime parameter override and restores the authored/shader default. An unknown parameter or invalid material throws.',
 'ResetProperties':'Removes all runtime parameter overrides, without deleting the material or changing its shader.',
 'HasOverride':'Reports whether this parameter has an explicit runtime override, not merely whether the shader declares it.',
 'ApplyTo':'Assigns existing Mesh/PrimitiveObject components on an entity. AllSlots is default; descendants are opt-in. Returns entities changed. Invalid handles/slots throw; primitive slots other than 0/AllSlots are rejected.',
 'Instantiate':'Returns an independent transient copy of effective material state, or an invalid handle if the source cannot resolve. Destroy the copy when finished.',
 'IsRuntime':'Returns whether the material is transient runtime-owned state, rather than a shared imported asset.',
 'Destroy':'For Material, destroys only transient state and returns false for imported assets/absent state. Every copied handle becomes invalid; renderables restore their fallback on update.',
 'AllSlots':'Marker for assignment to every slot: maximum size_t in C++, -1 in Lua. Actual slot indices are zero-based.',
 'Load':'Returns a UUID-backed handle without changing/importing files. The resulting handle may be invalid; validity depends on the active host and definition.',
 'Builtin':'Returns the selected precompiled built-in shader; default is StandardLit. Invalid preset values throw.',
 'IsAssetReady':'Returns true only if the database resolves the asset as ready; false covers unknown/missing/failed import states.',
 'GetAsset':'Returns optional AssetInfo for UUID or source path; absence becomes nil in Lua. It does not import files or open another project.',
 'LoadModel':'Returns optional imported model hierarchy data; absence/failure becomes nil in Lua. Call Scene.InstantiateModel to create entities from it.',
 'ScreenPointToRay':'Converts a top-left-origin presentation pixel coordinate through the specified game camera and its viewport to a world ray. presentationSize is the full surface pixel size.',
 'Pick':'Returns the nearest matching hit for a camera pixel point. Empty Target means no geometry hit; options apply layers, distance, enabled state and Filter.',
 'PickAll':'Returns matching camera-pixel hits ordered by distance. This is not a physics contact list.',
 'Raycast':'Returns the closest matching world-ray geometry hit. Empty Target means no hit. The ray direction must be nonzero and finite.',
 'RaycastAll':'Returns matching world-ray geometry hits in distance order. MaxDistance and Filter constrain the results.',
 'OverlapSphere':'Returns matching entities overlapping a world-space sphere; radius is nonnegative world distance. Uses scene query geometry, not physics contacts.',
 'OverlapBox':'Returns matching entities overlapping a world-axis-aligned box. Extents are half-size, not full dimensions.',
 'TryGetInheritedComponent':'Returns a local or opted-in nearest-ancestor component, or absence. Transform composition is handled by world-transform APIs, not by returning an ancestor Transform as the child.',
 'AddComponent':'Adds an absent component. Duplicate types and invalid entities fail. C++ forwards constructor arguments; Lua returns a copy that must be committed after editing.',
 'AddOrReplaceComponent':'Adds or replaces the specified component data. Identity/Hierarchy replacement is prohibited. Lua takes a type-name string and an already constructed value.',
 'HasComponent':'Returns whether the entity contains that component type. C++ invalid handles return false.',
 'GetComponent':'Returns the existing component. Missing/invalid entity fails. C++ returns a reference; Lua returns a copy.',
 'TryGetComponent':'Returns a component pointer in C++ or copied value in Lua, and null/nil if absent. Unknown Lua type strings are errors rather than absence.',
 'RemoveComponent':'Removes an optional component. Identity/Hierarchy/Name/Transform cannot be removed; C++ enforces those restrictions at compile time.',
 'IsComponentEnabled':'Reports an existing optional component enable state. Required defaults are always enabled in the editor/Lua; C++ prevents those queries through this template.',
 'SetComponentEnabled':'Changes an existing optional component enable flag, preserving its values. Required defaults cannot be disabled.',
 'SetComponent':'Lua commits a copied component into an existing component of the named type. Identity/Hierarchy replacement is rejected; absent components must first be added.',
 'GetId':'Returns the transient registry entity ID, not the persistent UUID. Do not save it as a cross-scene identifier.',
 'GetScene':'Returns the owning Scene pointer/value; this does not transfer ownership or make it survive scene destruction.',
 'AddChild':'Equivalent to parenting the supplied child to this entity, preserving world position by default. Returns success/failure.',
 'GetRegistry':'Advanced C++ access to EnTT storage. Prefer entity/scene operations for hierarchy invariants; never rewrite Identity/Hierarchy pools as ordinary user data.',
 'GetEntityUUID':'Returns the UUID text bound to this behavior by the runtime, not the entity display name.',
 'Resolve':'Looks up this UUID in the active scene, returning an invalid entity when no matching active-scene object exists.',
 'TryGetWorldTransform':'Returns success and fills a world Transform if the active-scene reference resolves. Lua returns (success, transform); do not use the transform when false.',
 'GetMatrix':'Returns this local Transform as a translation×rotation×scale matrix. This alone does not compose ancestors.',
 'GetForward':'Returns rotation-applied local negative Z direction.', 'GetRight':'Returns rotation-applied local positive X direction.', 'GetUp':'Returns rotation-applied local positive Y direction.',
 'Translate':'Adds the supplied vector to Position in its current coordinate space. A local Transform does not automatically translate in camera-relative coordinates.',
 'Rotate':'For Transform, applies an axis-angle quaternion (angle in radians) before the current rotation and normalizes. For Quaternion, returns the rotated input vector; use a unit quaternion.',
 'FromAxisAngle':'Constructs a quaternion from an axis and radians, normalizing the axis. Supply a nonzero axis for meaningful rotation.',
 'FromEuler':'Constructs a normalized quaternion from X/Y/Z Euler angles in radians, using Z×Y×X rotation composition.',
 'Conjugated':'Returns (-X, -Y, -Z, W). For a unit quaternion this equals its inverse.',
 'Slerp':'Returns shortest-path spherical interpolation between rotations by amount. Use normalized inputs; amount is not clamped.',
 'Cross':'Returns the 3D cross product, perpendicular to both input vectors according to the right-hand rule.',
 'XYZ':'Returns a Vec3 copy of the first three coordinates, discarding W.',
 'Determinant':'Returns the 3×3 determinant. A near-zero result indicates no usable inverse under this implementation threshold.',
 'Data':'Returns a pointer to row-major matrix floats. It points into this value and must not outlive it. Not exposed to Lua.',
 'Translation':'Returns an identity-based matrix with the supplied translation in the last column.',
 'Scaling':'Returns a matrix with the supplied X/Y/Z scale on the diagonal.',
 'Rotation':'Returns the matrix for a normalized form of the supplied quaternion.',
 'Transform':'Returns Translation(position)×Rotation(rotation)×Scaling(scale), acting on column vectors.',
 'Perspective':'Returns a right-handed perspective matrix. FOV is vertical radians; aspect must be positive; near/far must be distinct valid clipping distances. This math helper does not validate every projection input.',
 'Orthographic':'Returns a right-handed orthographic matrix for left/right/bottom/top and near/far extents. Avoid equal/opposed zero-width bounds.',
 'LookAt':'Returns a view matrix from eye toward target using up. Supply distinct eye/target and an up vector not parallel to their direction.',
 'TryInverse':'Returns false if the matrix is singular under Epsilon; C++ fills an output Mat4 on success. Lua returns (success, inverse).',
 'Decompose':'Attempts to split a matrix into translation, quaternion rotation and scale. Returns false for unusable homogeneous/scale values. Lua returns (success, position, rotation, scale); not every sheared matrix is representable as TRS.',
 'TransformPoint':'Transforms a point with homogeneous W=1 and divides by the resulting W where usable. Translation affects the point.',
 'TransformDirection':'Transforms a direction with W=0, so translation does not affect it. Scale/rotation can change its length; normalize explicitly if needed.',
 'ToRadians':'Converts an angle in degrees to radians: degrees×Pi/180.', 'ToDegrees':'Converts radians to degrees: radians×180/Pi.',
 'Clamp':'Returns a value constrained to minimum/maximum. Supply minimum ≤ maximum.',
 'IsNearlyEqual':'Reports absolute difference ≤ tolerance (default Epsilon). Use instead of exact floating equality for approximate calculations.',
 'Pi':'Floating-point π constant used for angle conversion.', 'Epsilon':'Small comparison threshold, 1e-6; used by normalization and inverse checks.',
 'Root':'Returns the all-zero UUID, representing the permanent scene root. Do not use it as a real asset ID.',
 'Generate':'Returns a random version-4-style nonzero UUID. It does not create or register an asset/entity.',
 'TryParse':'Accepts 32 hexadecimal digits with optional hyphens. Returns false for malformed text; Lua returns (success, UUID), while C++ fills the output only on success.',
 'Parse':'Lua-only convenience that returns a UUID or raises an error for malformed text.',
 'ToString':'Returns canonical hexadecimal UUID text with hyphens. Lua also supports tostring(uuid).',
 'GetHigh':'Returns the high 64 bits. Prefer text for external storage to avoid number precision/representation mistakes.',
 'GetLow':'Returns the low 64 bits. Prefer text for external storage to avoid number precision/representation mistakes.',
 'IsRoot':'Returns true when both halves are zero. Zero may be a scene root but is not a valid ordinary asset ID.',
 'GetActiveScene':'Returns the currently active hosted scene or null/nil when no scene is bound. The pointer is non-owning.',
 'LoadScene':'Requests loading by source path or asset UUID at a safe frame boundary. Returns whether accepted; inspect GetLastError on rejection. Acceptance is not synchronous completion.',
 'IsLoadPending':'Returns whether the host has a deferred scene load waiting to be processed.',
 'SetParameter':'Adds or replaces one named post-process parameter and returns its stored reference. Empty names throw. Lua edits Parameters on its copied effect value instead.',
 'AddEffect':'Appends an effect with nonzero shader UUID and optional name. C++ returns a reference; Lua returns a copy. Empty/zero shader UUID throws.',
 'RemoveEffect':'Removes the effect at a zero-based stack index; out-of-range indices throw.',
 'GetEffects':'Returns the effects vector in C++; Lua returns a one-based list of copied effect values. Add/remove operations can invalidate C++ references.',
 'IsEmpty':'Reports whether the custom-effect stack has no entries.',
 'SetEffect':'Lua writes an effect copy back at a zero-based stack index. Invalid index raises an error; commit the containing camera afterward.',
 'GetComponents':'Returns the host serializer component-codec registry; this does not expose scene saving/loading.',
 'Register':'Registers a codec for a Component-derived type, name and version. Serialize fills PropertyMap; deserialize returns success and receives the stored version for migration.',
 'RegisterDescriptor':'Registers an explicit codec descriptor. Ensure unique meaningful type/version data and valid callbacks; this is extension/tooling work.',
 'Find':'Returns a descriptor pointer for the registered type name, or null when absent. Later registration can invalidate pointers into the descriptor vector.',
 'GetDescriptors':'Returns the registry descriptor vector as a const reference. It does not return entities or live components.',
 'Bind':'ABI bridge sets the behavior entity UUID string. Generated host glue calls it; not an application/gameplay initialization entry point.',
 'BindTime':'ABI bridge binds host time state. Only host glue should call this; gameplay uses Time static methods.',
 'BindInput':'ABI bridge binds host input state. Only host glue should call this; gameplay uses Input static methods.',
 'GetView':'Protected derived-system helper returning an EnTT view for its component template types. Do not destroy required storage while iterating.',
 'AreComponentsEnabled':'Protected helper tests that all selected view components on the entity are enabled. Derived systems must choose to use this check.',
 'GetEntities':'Lua returns a one-based list of scene entities, optionally requiring a named component. Includes the root; traversal order is not stable.',
 'SetSystemEnabled':'Lua changes the named registered system enable state without removing its table. Unknown names fail.',
 'IsSystemEnabled':'Lua reports the named registered system enable state; unknown names fail.',
 'Get':'Lua matrix element read at zero-based row/column, returning a number. Indices outside the matrix dimensions raise an error.',
 'Set':'Lua matrix element write at zero-based row/column. Invalid indices raise an error; this modifies the matrix value.',
 'Id':'Persistent UUID of the asset/model, not a filesystem path or scene registry integer.',
 'NoMesh':'Sentinel uint32 maximum indicating this imported node has no mesh.', 'EntireAsset':'Sentinel uint32 maximum selecting the complete model rather than one glTF source node.',
 'CurrentFormatVersion':'Current persistence schema version constant. Do not assume changing it automatically migrates stored data.'
}

TIME = {
 'GetDeltaTime':'Returns scaled seconds for the current frame, or current fixed-step delta inside a fixed callback. Time scale zero yields no scaled progress.',
 'GetUnscaledDeltaTime':'Returns unscaled frame/fixed elapsed seconds; suitable for timers intended to ignore slow motion.',
 'GetSmoothDeltaTime':'Returns the smoothed scaled frame delta. It is not a fixed-step duration.',
 'GetTime':'Returns scaled elapsed simulation seconds; during fixed callbacks it uses fixed elapsed time.',
 'GetUnscaledTime':'Returns unscaled elapsed simulation seconds; during fixed callbacks it uses fixed unscaled time.',
 'GetRealtimeSinceStartup':'Returns monotonic real elapsed seconds since host time-state startup, independent of simulation scaling.',
 'GetFrameCount':'Returns the number of host-advanced gameplay frames, not an FPS estimate.',
 'GetTimeScale':'Returns current nonnegative scale: 1 normal, 0 paused, a fraction slow motion.',
 'GetFixedDeltaTime':'Returns configured scaled fixed-step seconds (default 0.02). Changing scale does not automatically rewrite this setting.',
 'GetFixedUnscaledDeltaTime':'Returns configured fixed delta divided by positive scale; returns zero at scale zero.',
 'GetFixedTime':'Returns accumulated scaled fixed-step seconds.', 'GetFixedUnscaledTime':'Returns accumulated unscaled fixed-step seconds.',
 'IsInFixedTimeStep':'Reports whether code is currently executing a host fixed-step callback.',
 'SetFixedDeltaTime':'Sets fixed-step seconds. Requires finite value ≥0.0001; otherwise throws invalid_argument.',
 'SetTimeScale':'Sets finite, nonnegative simulation scale. Zero stores the previous positive scale for Resume; negative/non-finite values throw.',
 'SetSlowMotion':'Sets a scale in (0,1], default 0.5. Non-finite or out-of-range values throw.',
 'RestoreTimeScale':'Sets scale to 1, restoring normal simulation speed rather than the remembered pre-pause scale.',
 'Pause':'Sets simulation scale to zero. Editor rendering/input can still be processed by the host.',
 'Resume':'Restores the remembered positive scale, including an earlier slow-motion scale.',
 'IsPaused':'Returns true when time scale is exactly zero.',
 'GetMaximumDeltaTime':'Returns the maximum scaled frame delta used to cap simulation jumps after hitches (default 1/3 second).',
 'SetMaximumDeltaTime':'Sets a finite positive maximum delta; zero, negative or non-finite values throw.'
}

INPUT = {
 'IsActive':'Returns whether gameplay currently receives focused input. Text editing/editor navigation should not be mistaken for game controls.',
 'GetKey':'Returns true while the physical KeyCode is held, or false without gameplay focus.',
 'GetKeyDown':'Returns true only on the frame a key changes from released to pressed; use for one-shot actions.',
 'GetKeyUp':'Returns true only on the frame a key changes from pressed to released.',
 'GetMouseButton':'Returns whether the named mouse button is held while gameplay input is active.',
 'GetMouseButtonDown':'Returns the button press edge for this frame.', 'GetMouseButtonUp':'Returns the button release edge for this frame.',
 'GetMousePosition':'Returns the gameplay mouse position supplied by the host, or zero when inactive.',
 'GetMouseDelta':'Returns the current frame mouse movement delta, or zero when inactive.',
 'GetScrollDelta':'Returns the frame scroll delta, or zero when inactive.',
 'GetAnyKey':'Reports any pressed/held key or mouse button while gameplay input is active.',
 'GetAxis':'Horizontal is D/right minus A/left; Vertical is W/up minus S/down. Mouse and scroll axes return the corresponding frame delta coordinate.'
}

FIELDS = {}
def fields(types, **values):
    for type_ in types.split('|'): FIELDS.setdefault(type_, {}).update(values)

fields('Vec2|Vec3|Vec4|Quaternion',X='First coordinate (quaternion X is not an Euler angle).',Y='Second coordinate.',Z='Third coordinate.',W='Fourth coordinate; quaternion identity W is 1, a homogeneous point uses W=1, a direction W=0.')
fields('Mat3|Mat4',Values='Row-major float array, identity by default. C++ raw access is unchecked; Lua uses bounds-checked Get/Set methods.')
fields('Transform|ModelAssetNode',Position='Local translation relative to the parent; zero by default.',Rotation='Local quaternion rotation; identity by default. Use radians in quaternion helper constructors.',Scale='Local component-wise scale; one on each axis by default. Zero scale can make world-to-local/inverse operations impossible.')
fields('Name',Value='Display label, initially empty. It is not required to be unique and is not stable identity.')
fields('Identity',Value='Scene-assigned stable entity UUID. Treat it as read-only; change names instead of changing identity.')
fields('Hierarchy',Parent='Parent entity UUID, zero for the permanent root relation.',Children='Direct child UUID list managed by Scene parenting operations. Do not edit it to reparent an entity.')
fields('AssetInfo',SourcePath='Source asset filesystem path; Lua exposes UTF-8 text.',MetaPath='Matching .meta sidecar path.',CachePath='Generated runtime/import cache path.',Importer='Importer name used for this asset.',ImporterVersion='Version of the importer output schema, used for cache invalidation.',State='AssetState describing import readiness.',LastError='Diagnostic text for a failed import; inspect State before using cached data.')
fields('CameraViewport',X='Normalized bottom-left X coordinate, default 0.',Y='Normalized bottom-left Y coordinate, default 0.',Width='Width fraction of full presentation surface, default 1.',Height='Height fraction of full presentation surface, default 1.')
fields('Camera',Projection='Perspective or Orthographic projection.',VerticalFieldOfView='Perspective vertical FOV in radians; default 60 degrees converted to radians.',OrthographicSize='Vertical orthographic extent setting; only used in Orthographic projection.',NearPlane='Near clipping distance, default 0.1. Geometry closer than it is clipped.',FarPlane='Far clipping distance, default 1000. Geometry beyond it is clipped.',AspectRatio='Fixed width/height ratio, default 16/9; used with Fixed AspectMode.',AspectMode='Automatic follows viewport dimensions; Fixed uses AspectRatio.',Viewport='Normalized target rectangle, full screen by default.',Priority='Lower priorities render first; equal priorities use UUID order.',Active='Camera participation flag, independent of component Enabled. Both must allow rendering.',ClearColor='Camera background/clear RGBA color.',PostProcessing='Nested CameraPostProcessing settings for this camera image.')
fields('CameraPostProcessing',Enabled='Master camera post-processing enable flag.',Bloom='Enables glow from bright rendered regions.',AmbientOcclusion='Enables screen-space ambient occlusion.',AntiAliasingMode='None, FXAA or TAA edge smoothing.',ToneMappingMode='Linear, Filmic or ACES output mapping.',Exposure='Exposure compensation in EV stops; 0 is neutral.',DepthOfField='Nested focus/lens blur settings. Disabled by default.',CustomEffects='Ordered custom full-screen effect stack.')
fields('CameraPostProcessing::DepthOfFieldSettings',Enabled='Enables this depth-dependent blur effect; also requires camera post-processing.',FocusDistance='World-space focus distance, default 10. Surfaces away from it blur.',Aperture='Lens f-stop, default 16. Lower values increase blur, independently of exposure compensation.',ShutterSpeed='Exposure-model shutter time in seconds, default 1/125.',Sensitivity='Exposure-model sensitivity (ISO), default 100.',CocScale='Circle-of-confusion blur scale multiplier, default 1.',CocAspectRatio='Circle-of-confusion aspect multiplier, default 1.',MaxApertureDiameter='Maximum aperture-diameter setting, default 0.01.',Quality='Low, Medium or High quality tier.',NativeResolution='Whether to use native-resolution DoF processing, default false.')
fields('Light',Type='Directional, Sun, Point or Spot.',Color='Linear RGB light color, white by default.',Intensity='Lux for Directional/Sun; lumens for Point/Spot. Default 1000.',Range='World-space spherical falloff radius for Point/Spot, default 10; ignored for Directional/Sun.',InnerConeAngle='Spot inner cone half-angle in radians, default 20 degrees.',OuterConeAngle='Spot outer cone half-angle in radians, default 30 degrees; keep it ≥ inner.',SunAngularRadius='Sun disk angular radius in radians, default 0.00935; Sun only.',SunHaloSize='Sun halo size factor, default 10; Sun only.',SunHaloFalloff='Sun halo falloff factor, default 80; Sun only.',CastShadows='Enables shadow casting from this light, where the renderer supports the chosen light configuration.')
fields('Mesh|PrimitiveObject',MaterialAsset='Global material override UUID; zero preserves imported/built-in material.',LayerMask='Renderable layer bits, default 0xff. Not a physics collision-layer setting.',Visible='Game render visibility; separate from the editor-only hidden flag.',CastShadows='Whether this geometry casts shadows from shadow-capable lights.',ReceiveShadows='Whether this geometry receives rendered shadows.')
fields('Mesh',MeshAsset='Imported mesh/model asset UUID.',ModelNodeIndex='glTF source node index, or EntireAsset for a complete mesh/model.',Materials='Optional per-slot material UUID overrides. Global MaterialAsset takes precedence.')
fields('PrimitiveObject',Shape='Selects which procedural dimensions apply.',Size='Cube X/Y/Z geometry size before entity Transform scale.',Radius='Sphere/Cylinder/Capsule/Cone radius before Transform scale.',Height='Cylinder/Capsule/Cone height setting.',Width='Plane width.',Depth='Plane depth.',MajorRadius='Torus ring center radius.',MinorRadius='Torus tube radius.',Segments='Circumferential geometry subdivisions, default 32.',Rings='Latitudinal/tube subdivisions, default 16.',Color='Linear RGBA fallback surface color; a material override supplies its own parameters.')
fields('SceneQueryBounds',Shape='Box or Sphere query geometry.',Center='Local-space center offset.',Extents='Local box half-size, default 0.5 per axis.',Radius='Local sphere radius, default 0.5.',LayerMask='Query layer bits, default all bits set.')
fields('GaussianBlur',Size='Blur-radius multiplier in pixels; default 1. Zero leaves the image unchanged.')
fields('Vignette',Color='Edge effect RGBA color, black by default.',Intensity='Effect strength, default 0.35.',Smoothness='Edge transition smoothness, default 0.35.',Roundness='Shape roundness parameter, default 1.')
fields('ModelInstance|ModelNode',ModelAsset='Original imported model asset UUID; nodes remain scene entities, not separate assets.')
fields('ModelNode|ModelAssetNode',SourceIndex='Stable source glTF node index.',MeshIndex='Imported mesh index or no-mesh sentinel.',StablePath='Importer path identifying a model node across hierarchy changes.')
fields('ModelNode',HasMesh='Boolean import flag indicating this child contains renderable mesh geometry.')
fields('ModelAssetNode',Name='Node display name from the imported model.',Children='Zero-based node indices for direct children.',MaterialNames='Imported material labels, not runtime Material handles.')
fields('ModelAsset',Name='Imported model display name.',Nodes='Node descriptors used to instantiate the scene hierarchy.',Roots='Zero-based node indices at the model root.')
fields('ScriptPropertyValue',Name='Property declaration name.',Type='Serialized property type spelling.',Value='Serialized Inspector override text; not evaluated as arbitrary source code.')
fields('ScriptAttachment',Source='Script asset UUID text used to resolve executable module/bytecode.',TypeName='Unique declared behavior name.',Lua='True for Lua bytecode attachments, false for native C++ behavior modules.',Properties='Authored property override records.')
fields('ScriptComponents|UnresolvedComponents',Values='Stored attachment/unresolved-component records, preserved with scene authoring data.')
fields('SceneRay',Origin='World-space ray start point.',Direction='World-space nonzero direction, default negative Z.')
fields('SceneQueryOptions',LayerMask='Allowed query layer bits, all set by default.',MaxDistance='Maximum ray distance in world units; infinity by default.',IncludeDisabled='Opt-in inclusion of disabled query/renderable components, default false.',Filter='Optional predicate accepting an Entity and returning whether it is eligible. Avoid mutating scene storage inside the filter.')
fields('SceneQueryHit',Target='Hit entity; invalid means no hit.',Point='World-space hit point.',Normal='World-space hit surface normal.',Distance='Distance from the query ray origin in world units.',StartedInside='Whether the ray began inside the tested shape.')
fields('SceneEnvironment',Mode='Map or Material mode selects which source field applies.',SourceAsset='Environment map asset UUID.',MaterialAsset='Environment background material asset UUID.',ClearColor='Fallback linear RGBA color when no visible skybox applies.',Rotation='Environment IBL rotation in radians; not an entity Transform.',Intensity='Environment illumination intensity in lux, default 30000.',ImageBasedLighting='Whether an environment map supplies illumination; independent of sky visibility.',SkyboxVisible='Whether the environment background is visible; hiding it does not turn off IBL.',ShowSun='Environment skybox sun display option, default false.')
fields('MaterialRenderState',Override='Whether instance state overrides are active. SetRenderState forces true; ResetRenderState restores false.',DoubleSided='Double-sided lighting override. The shader must compile this capability; culling is a separate setting.',DepthTest='Enable depth testing for this surface.',DepthWrite='Allow this surface to write depth. Does not enable transparent blending.',ColorWrite='Allow color writes to the target.',Culling='Faces to exclude from rendering.',DepthFunction='Depth comparison function, default LessEqual in the override value.')
fields('ShaderParameter',Name='Declared parameter name passed to Material setters.',Type='Reflected ShaderParameterType.')
fields('PostProcessParameter',Name='Shader parameter name.',Type='Value storage kind.',Value='Float/Vec2/Vec3/Vec4 value packed into Vec4.',IntegerValue='Integer payload when Type is Integer.',BooleanValue='Boolean payload when Type is Boolean.',TextureAsset='Texture asset UUID when Type is Texture.')
fields('CustomPostProcessEffect',ShaderAsset='Precompiled full-screen shader asset UUID.',Name='Optional effect label, not identity.',Order='Execution order in the camera effect pipeline.',Parameters='Named typed parameter values supplied to the shader.')
fields('ProjectMetadata',ProjectUUID='Stable project UUID, generated by default.',Name='Project display name, Untitled by default.',AssetDirectory='Project-relative asset folder, Assets by default.',StartupScene='Configured startup scene path.',Properties='Namespaced string settings, e.g. renderer.pipeline, for extensible project metadata.')
fields('SerializedComponent',Type='Registered serializer/component type name.',Version='Stored schema version passed to the decoder.',Properties='String property map for the component codec.')
fields('ComponentSerializationRegistry::Descriptor',Type='Stable codec registration name.',Version='Codec schema version.',Serialize='Callback (Entity, PropertyMap&) returning whether data was serialized.',Deserialize='Callback (Entity, PropertyMap, storedVersion) returning successful reconstruction.')
fields('ScriptModuleApi',AbiVersion='Module ABI version; must match ScriptAbiVersion supported by the host.',TypeName='Declared behavior type name.',Create='ABI callback allocating an instance for entity UUID text; returns opaque instance pointer.',Destroy='ABI callback releasing an instance allocation after lifecycle cleanup.',OnCreate='ABI callback forwarding creation lifecycle for an opaque instance.',OnUpdate='ABI callback forwarding frame delta seconds for an opaque instance.',OnDestroy='ABI callback forwarding gameplay cleanup before allocation destruction.',SetProperty='ABI callback (instance, property name, serialized value) returning successful assignment.')

SPECIAL = {
 ('ComponentInheritance','Mode'):'Compile-time inheritance policy, None by default. Explicitly specialize the trait for nearest-ancestor fallback if your component needs it.',
 ('ComponentInheritance<Transform>','Mode'):'Compile-time Composed policy. Use world-transform APIs for this specialized composed relationship.',
 ('Entity','Id'):'C++ alias for the 32-bit transient registry ID. It is not the 128-bit persistent UUID.',
 ('Entity','IsValid'):'Returns whether the handle currently names a live registry entity. The registry itself must still be alive; this is not a lifetime-safe pointer after scene destruction.',
 ('UUID','IsValid'):'Returns true for a nonzero UUID. It does not check whether that asset/entity exists. The all-zero root UUID returns false here.',
 ('EntityReference','IsValid'):'Resolves the UUID against the active scene and returns whether its world Transform can be read.',
 ('SceneEnvironment','IsValid'):'Validates supported mode, finite nonnegative intensity, finite rotations/colors, nonnegative color channels and alpha in [0,1]. Does not validate asset import readiness.',
 ('Material','IsValid'):'Returns whether the current host resolves the material definition. A destroyed transient handle becomes invalid.',
 ('Shader','IsValid'):'Returns whether the current host resolves the shader definition/reflection. This does not run GPU compilation.',
 ('Material','Create'):'Creates an owned transient Material from a shader or built-in preset (default StandardLit). Unresolvable Shader input returns an invalid handle. Check IsValid; Destroy valid owned results when finished.',
 ('MaterialBuilder','SetShader'):'Chooses the precompiled shader to use at Build. Returns the builder for chaining. Existing queued setters are validated against the new layout when built.',
 ('MaterialBuilder','SetRenderState'):'Queues render-state overrides to apply during Build and returns the builder for chaining.',
 ('MaterialBuilder','Clear'):'Clears queued parameter/state overrides but retains the selected shader.',
 ('Scene','Clear'):'Destroys ordinary scene entities while preserving the permanent root; does not clear the project asset database.',
 ('PostProcessingStack','Clear'):'Removes every custom effect from the stack, not the camera or built-in post-processing settings.',
 ('Scene','TryGetInheritedComponent'):COMMON['TryGetInheritedComponent'],
}

PARAMETERS = {
 'name':'Display label or declared parameter/type name as specified by this method; names are not UUID identity.',
 'id':'Stable UUID or transient integer ID, according to the overload.', 'uuid':'Explicit stable UUID; keep it unique in the target scene.',
 'entity':'Entity in the relevant live scene; must not outlive that scene.', 'child':'Entity whose parent relationship is being changed.', 'parent':'Target parent entity in the same scene.',
 'worldPositionStays':'True preserves world placement; false preserves local values. Default true.',
 'transform':'Complete Transform value in the coordinate space named by the method.',
 'translation':'Translation vector in the current Transform/local space.', 'axis':'Rotation axis; use a nonzero direction.', 'radians':'Angle in radians, not degrees.',
 'rotation':'Quaternion rotation, preferably normalized.', 'position':'Translation/world center as specified by the operation.', 'scale':'Scale vector or time multiplier as specified by the operation.',
 'value':'Value whose type must match the declaration. Numeric values must be finite where validated.',
 'shader':'Precompiled shader definition handle, not a source string.', 'source':'Existing material whose compatible effective values are copied.',
 'preserveProperties':'Retain matching name/type parameter values when switching shaders; default true.', 'copyRenderState':'Also copy source render state; default true.',
 'state':'Complete abstract MaterialRenderState value.', 'slot':'Zero-based material slot; AllSlots/-1 selects all slots.', 'includeChildren':'Apply through descendants only when true; default false.',
 'camera':'Entity with an eligible Camera component in this scene.', 'screenPoint':'Top-left-origin pixel coordinate in the full presentation surface.', 'presentationSize':'Full presentation width/height in pixels, not normalized coordinates.',
 'options':'Optional SceneQueryOptions controlling layer mask, distance, disabled state and predicate.', 'ray':'World origin/direction describing a nonzero ray.',
 'center':'World-space center of an overlap query.', 'radius':'Sphere radius in world distance units.', 'extents':'Box half-size (not full size).',
 'model':'Imported ModelAsset description.', 'modelAsset':'Model asset UUID resolved through AssetManager.', 'path':'Filesystem source path; in Lua use UTF-8 text.', 'assetId':'Scene asset UUID.',
 'type':'Stable codec registration name or Lua component type string.', 'version':'Serialization schema version number.', 'storedVersion':'Stored schema version for decoding/migration.',
 'serialize':'Callback that writes named string values to PropertyMap.', 'deserialize':'Callback that reads property values and returns decode success.', 'descriptor':'Complete registration record and callbacks.',
 'args':'Constructor arguments forwarded to the component/system type.', 'enabled':'New enable state; false preserves values rather than removing them.',
 'seconds':'Finite elapsed/step duration in seconds; see this method for allowed limits.', 'deltaTime':'Scaled frame duration in seconds.', 'fixedDeltaTime':'Scaled fixed-step duration in seconds.',
 'column':'Zero-based column index.', 'row':'Zero-based row index.', 'columns':'Number of grid columns; must be positive.', 'rows':'Number of grid rows; must be positive.',
 'from':'Interpolation starting value.', 'to':'Interpolation target value.', 'amount':'Interpolation factor; 0 starts and 1 ends, not automatically clamped.',
 'left':'First operand or left projection extent.', 'right':'Second operand or right projection extent.', 'minimum':'Inclusive lower limit.', 'maximum':'Inclusive upper limit.', 'tolerance':'Allowed absolute difference; default Epsilon.',
 'verticalFieldOfViewRadians':'Vertical perspective FOV in radians.', 'aspectRatio':'Width divided by height, positive.', 'nearPlane':'Positive near clipping distance.', 'farPlane':'Far clipping distance greater than near.',
 'bottom':'Bottom orthographic extent.', 'top':'Top orthographic extent.', 'eye':'World-space camera/view origin.', 'target':'World-space point being looked at.', 'up':'Nonparallel reference up direction.',
 'point':'Position treated as homogeneous W=1.', 'direction':'Direction treated as W=0.', 'result':'Output value filled on success.', 'text':'UUID text: hexadecimal digits with optional hyphens.',
 'high':'High 64-bit UUID half.', 'low':'Low 64-bit UUID half.', 'x':'X coordinate.', 'y':'Y coordinate.', 'z':'Z coordinate.', 'w':'W coordinate.', 'xyz':'First three coordinates.',
 'diagonal':'Initial matrix diagonal; other entries are zero.', 'parameter':'Named typed post-process parameter to add/replace.', 'shaderAsset':'Precompiled custom-effect shader UUID, nonzero.', 'index':'Zero-based effect vector index.',
 'sourcePath':'Asset source filesystem path in the active database.', 'key':'Physical KeyCode constant.', 'button':'MouseButton constant.', 'preset':'ShaderPreset constant; StandardLit is the default.',
 'behavior':'Behavior instance owned by the host/module glue.', 'state':'Host/render-state value, according to the signature.', 'entityUUID':'Bound entity UUID text, not display name.',
 'registry':'Advanced live EnTT component registry owned by the scene.', 'view':'EnTT component view used by a derived system.'
}

def describe(type_, member):
    name = member['name']
    if (type_,name) in SPECIAL: return SPECIAL[type_,name]
    if type_ == 'Time' and name in TIME: return TIME[name]
    if type_ == 'Input' and name in INPUT: return INPUT[name]
    if name in FIELDS.get(type_, {}): return FIELDS[type_][name]
    if member['kind'] == 'alias' and type_ == 'MathFunctions': return 'Alternative C++ spelling for the math type shown in this alias declaration. Lua uses Vec2/Vec3/Vec4/Mat3/Mat4 directly.'
    if member['kind'] == 'value':
        if type_ == 'KeyCode': return f'Physical {name} key identifier. Use Input.GetKey/GetKeyDown/GetKeyUp with it, not an operating-system key number.'
        return ENUMS.get(type_, {}).get(name) or f'{name} option of {type_}. See the type explanation for how this choice is used.'
    if name == type_.split('::')[-1] or name.startswith('~'):
        if '= delete' in member['signature']: return 'This construction/copy/assignment is deleted: the host service is not constructible or this object is non-copyable.'
        return 'Constructs a value using the shown defaults/arguments, or destroys it when its lifetime ends. A value/handle is not ownership of the active host.'
    if name.startswith('operator'):
        return OPERATORS.get(name, 'The declaration specifies the operands/result. This operator is not a scene update or renderer operation.')
    if type_ in ('Material','MaterialBuilder') and re.fullmatch(r'(Set|Get)(Float|Vec[234]|Color|Matrix[34]|Integer|Boolean|Texture)', name):
        setter = name.startswith('Set');kind = name[3:]
        return (f'Queues a named {kind} value and returns this builder; Build validates it against the selected shader.' if type_ == 'MaterialBuilder' else f'{"Writes" if setter else "Returns"} a named {kind} parameter. Missing name, mismatched reflected type or invalid material throws. Numeric setters reject non-finite values; texture setters use a supported project image asset UUID (zero clears the reference).')
    if type_ == 'PostProcessParameter' and name in ('Float','Float2','Float3','Float4','Integer','Boolean','Texture'):
        return f'Returns a named {name} parameter with the correct Type and typed payload. The effect shader must declare a compatible parameter.'
    if name in COMMON: return COMMON[name]
    raise ValueError(f'Missing reviewed API description: {type_}.{name}')

OPERATORS = {
 'operator+':'Returns vector addition; unary plus returns an unchanged value. Inputs are not modified.',
 'operator-':'Returns subtraction; unary minus negates the coordinates. Inputs are not modified.',
 'operator*':'Multiplies operands as declared: scalar scales a vector, vector×vector Vec3 is component-wise, matrix×matrix composes, matrix×vector transforms, quaternion×quaternion composes rotations.',
 'operator/':'Divides vector coordinates by a scalar. Supply a nonzero divisor; this low-level arithmetic does not guard division by zero.',
 'operator+=':'Adds into this vector and returns a C++ reference to it.', 'operator-=':'Subtracts into this vector and returns a C++ reference to it.',
 'operator*=':'Scales this vector in place and returns it by reference.', 'operator/=':'Divides this vector in place; the divisor must be nonzero.',
 'operator()':'C++ element access at zero-based row/column. Returns a reference on mutable matrices, a float on const matrices. Raw indexing is unchecked; Lua uses Get/Set.',
 'operatorbool':'Tests handle/hit validity or nonzero UUID. This does not make a non-owning entity safe after its scene lifetime ends.',
 'operator==':'Exact equality: UUID halves, entity registry+ID, or vector coordinates according to type. Use IsNearlyEqual for approximate floating comparison.',
 'operator!=':'Negates exact equality, not an approximate floating comparison.', 'operator<':'Orders UUID values lexicographically by high then low half.',
 'operator=':'Assignment/copy policy is given by this declaration; Scene assignment is deleted because scene-owned storage cannot be copied.'
}

ENUMS = {
 'AssetState':dict(Unknown='No established import state.',Ready='Imported runtime data is available.',NeedsImport='Source/cache state requires importing.',Missing='Expected source asset is missing.',Failed='Import failed; inspect AssetInfo.LastError.'),
 'ComponentInheritanceMode':dict(None_='No inherited component lookup.',NearestAncestor='If absent locally, use the first matching ancestor.',Composed='Combine hierarchical state through specialized APIs, as Transform does.'),
 'CameraProjection':dict(Perspective='Perspective projection: distant objects appear smaller.',Orthographic='Parallel projection: size does not shrink with distance.'),
 'CameraAspectMode':dict(Automatic='Derive aspect from the actual viewport.',Fixed='Use Camera.AspectRatio.'),
 'AntiAliasing':dict(None_='No camera anti-aliasing.',FXAA='Fast approximate spatial anti-aliasing.',TAA='Temporal anti-aliasing using frame history.'),
 'ToneMapping':dict(Linear='Linear output mapping.',Filmic='Filmic response curve.',ACES='ACES-style tone mapping.'),
 'LightType':dict(Directional='Distant direction-only light; intensity in lux.',Sun='Directional sun light with sun disk/halo controls; intensity in lux.',Point='Omnidirectional local light; intensity in lumens and Range in world units.',Spot='Local cone light; intensity in lumens, Range and cone half-angles apply.'),
 'InputAxis':dict(Horizontal='D/right minus A/left keyboard axis.',Vertical='W/up minus S/down keyboard axis.',MouseX='Current horizontal mouse delta.',MouseY='Current vertical mouse delta.',ScrollX='Current horizontal scroll delta.',ScrollY='Current vertical scroll delta.'),
 'SceneEnvironmentMode':dict(Map='Use the environment map SourceAsset for the environment.',Material='Use the MaterialAsset for the background; not a hidden direct light.'),
 'ShaderPreset':dict(StandardLit='Opaque lit PBR surface preset.',Unlit='Opaque surface not lit by scene lights.',StandardLitTransparent='Lit surface with compiled transparent blending.',UnlitTransparent='Unlit surface with compiled transparent blending.'),
 'MaterialCulling':dict(None_='Render front and back faces.',Front='Cull front faces.',Back='Cull back faces.',FrontAndBack='Cull both faces; normally no visible geometry.'),
 'MaterialDepthFunction':dict(Less='Pass only if incoming depth is less.',LessEqual='Pass for less or equal depth.',Equal='Pass for equal depth.',Greater='Pass for greater depth.',GreaterEqual='Pass for greater or equal depth.',Always='Always pass comparison.',Never='Never pass comparison.',NotEqual='Pass for different depth.')
}
ENUMS.update({
 'PrimitiveShape':dict(Cube='Box geometry controlled by Size (X/Y/Z).',Sphere='Sphere controlled by Radius, Segments and Rings.',Cylinder='Closed cylinder along local Y, controlled by Radius, Height and Segments.',Capsule='Cylinder plus rounded end caps along local Y. Height is its central section; total length also includes two radii.',Plane='Flat local X/Z rectangle controlled by Width and Depth, facing local +Y.',Cone='Cone along local Y with a base cap, controlled by Radius, Height and Segments.',Torus='Ring controlled by MajorRadius, MinorRadius, Segments and Rings.'),
 'SceneQueryShape':dict(Box='Use Center and box half-size Extents.',Sphere='Use Center and Radius.'),
 'DepthOfFieldQuality':dict(Low='Lower-quality processing tier favoring performance.',Medium='Intermediate quality/performance tier, the default.',High='Higher-quality processing tier with more rendering cost.'),
 'MouseButton':dict(Left='Primary left mouse button.',Middle='Middle/wheel button press (not scrolling).',Right='Secondary right mouse button.',Back='Back side button.',Forward='Forward side button.'),
 'ShaderParameterType':dict(Float='One floating-point value; SetFloat/GetFloat.',Float2='Two floating-point values; SetVec2/GetVec2.',Float3='Three floating-point values; SetVec3/GetVec3.',Float4='Four floating-point values; SetVec4/GetVec4 (Color is equivalent storage).',Integer='Signed 32-bit integer; SetInteger/GetInteger.',Boolean='Boolean true/false; SetBoolean/GetBoolean.',Texture2D='Supported project image asset UUID; SetTexture/GetTexture.',Matrix3='3×3 float matrix; SetMatrix3/GetMatrix3.',Matrix4='4×4 float matrix; SetMatrix4/GetMatrix4.'),
 'PostProcessParameterType':dict(Float='Scalar stored in Value.X.',Float2='Two floats stored in Value.X/Y.',Float3='Three floats stored in Value.X/Y/Z.',Float4='Four floats stored in Value.',Integer='Integer stored in IntegerValue.',Boolean='Boolean stored in BooleanValue.',Texture='Texture UUID stored in TextureAsset.'),
 'PropertyType':dict(Boolean='True/false authored property.',Integer='Integer authored property.',Float='Floating-point authored property.',String='Text authored property.',Vec2='Two-coordinate authored vector.',Vec3='Three-coordinate authored vector.',Vec4='Four-coordinate authored vector/color.',Entity='Entity-reference property selected/resolved by UUID.',Asset='Project asset-reference property selected/resolved by UUID.')
})
for values in ENUMS.values():
    if 'None_' in values: values['None'] = values.pop('None_')
