# Audio subsystem (0.6)

Public entry points are `Audio.h`, `AudioClip.h`, `AudioMixer.h`, and the two
component headers. They contain no miniaudio, device, Qt or editor-loop types.
`src/Audio` owns the implementation. miniaudio 0.11.23 is a private static
dependency in the core DLL, not a public SDK dependency or a second audio loop.
Fetch with `scripts/get_miniaudio.ps1` / `.sh`. Its bundled stb_vorbis supplies
Vorbis decoding. The release contains no FFmpeg process dependency.

## Ownership and lifecycle

Each private Engine owns one AudioRuntime. Public operations dispatch to the
most recently constructed live runtime (the same main-thread hosting model as
AssetManager). Creation does not open a device. Engine.StartScripts binds the
active scene and enables PlayOnAwake before user OnCreate; normal updates sync
after native/Lua gameplay changes. RenderEditorFrame only syncs authoring and
pauses gameplay transport; previews bypass that host pause. StopScripts, scene
replacement and Shutdown release voices before mixer groups and the engine.
Never access ECS or Python from audio callbacks. All public control calls and
runtime lifetime operations must run on the host/gameplay thread.

miniaudio owns its render callback and resource-manager worker threads. Short
clips share asynchronous decoded buffers. Requested streaming, or estimated
float PCM over 64 MiB, uses stream data sources. Streaming changes apply at the
next voice start. Metadata is cached for a playback session. File imports probe
real decoded frames and reject corrupt headers, over-2-GiB sources, unsupported
formats, >8 channels and >24-hour declarations. Paths stay filesystem-native;
Windows uses wide entry points. Our private Vorbis wide-path adapter preserves
pull decoding for length, seeking and streaming; the upstream adapter's default
wide-path fallback would use push decoding with unknown duration. The resource
manager's default-file VFS also receives a private pull-mode adapter: it
duplicates the open file for stb_vorbis, preserving seek/length/loop support
without loading the compressed file into RAM. Non-file/custom VFS sources keep
the upstream fallback and are not exposed by the public asset service. These
adapters depend on the pinned private miniaudio implementation, so codec
upgrades must run the Unicode streaming regressions on each platform. They are
initialized once before any decoder/resource-manager threads are started.

## Mixing

A logical voice has a dry 2D sound and a wet spatial sound, scheduled for the
same engine frame. Their gains use (1-SpatialBlend) and SpatialBlend. This
provides a real continuous blend, not an enabled/disabled spatialization flag.
Both paths share bus routing and transport; the spatial path supplies panning,
cones and Doppler. Distance attenuation is evaluated from source/listener
world positions on the host thread, with a logarithmic curve normalized to
zero at MaxDistance or piecewise-linear custom points. Units are metres and
degrees; transforms use -Z forward, +Y up. Transform scale does not change audio
radius. Teleports/long deltas reset velocity to avoid extreme Doppler spikes.

The enabled listener with highest Priority wins, then smallest UUID. Missing
listeners silence gameplay, never the editor preview. No camera is secretly
used as a listener. Source disabling/removal destroys its voice; enable starts
a new PlayOnAwake lifecycle. Finished/stolen/stopped voices do not repeatedly
restart each tick. Handles use monotonically increasing IDs across sessions,
not reusable component pointers. Entity-owned voice settings are re-snapshotted
each tick; runtime fades are separate gain envelopes. Audio uses unscaled real
delta, independently of Time scale.

64 logical voices by default, configurable 1–1024. Smaller source priority
numbers win; the farthest source breaks equal-priority victim ties. A new lower
priority source cannot steal a higher priority one. One logical voice has two
native branches; do not report the logical limit as a hardware voice limit.

Each mixer bus is a native sound group with graph routing. UUID zero is Master;
stable SFX and Music UUIDs are predeclared. Validate parent existence, duplicate
IDs, all cycles, gain/pitch ranges and 256-bus limit before publishing changes.
Missing source bus IDs fall back to Master. Solo includes ancestors and
descendants of solo branches. Bus mute/gain/pitch compose natively; pause
preserves transport per voice. Reattach nodes only if routing actually changes,
not every frame. Destroy voices first; detach groups before freeing them so
UUID map order never becomes node-destruction order.

`.bmixer` version 1 is YAML containing a Buses sequence of Id, Parent, Name,
Volume, Pitch, Muted, Solo and Paused. The selected listener's MixerAsset selects
an imported mixer. Runtime loading stages a complete graph and preserves live
voice cursors. Editor saving validates and atomically replaces the source, then
refreshes just that asset. Runtime bus setters never write project files.

## Editor and bindings

AudioClipImporter validates/caches audio with UUID metadata. AudioMixerImporter
validates the graph before caching. Inspector fields and native setters use
the same source validation, typed clip pickers reject unrelated assets, and
Lua's generic Entity component operations cover AudioSource/AudioListener.
LuaAudio.cpp owns audio bindings. Script AudioClip properties are UUID handles
in both native reflection wrappers and declarative Lua PROPERTY.

Preview controls call the same native decoder/playback pipeline, not Qt Media
or the OS file player. One preview at a time, stopped on inspector selection,
scene/project changes, Play and editor teardown. Icons are rasterized from the
provided SVGs at build time. Scene-only guide spheres use Min/MaxDistance; cone
guides use the source direction and full cone angle. No audio icon/guide belongs
to a Game view. Picking includes enabled audio icons.

## Verification and extension

`BazzaltAudioTests` renders real float PCM with a no-device miniaudio engine,
testing transport, output gain, disabled listeners, distance falloff, buses,
voice stealing, Unicode assets, serialization and teardown. Codec fixtures
come from the pinned dependency where applicable. The normal runtime still
uses hardware; no-device mode is private testing configuration, not a silent
fallback falsely reporting successful playback.

For a new public field/operation: update C++, source/listener validation,
AudioSerialization, AudioBridge inspection/setters, LuaAudio, script property
conversion where relevant, editor ranges/locales, PCM/UI tests and 0.6 docs.
For another codec, add and pin a real decoder backend, wire Unicode and stream
paths, and test decode/seek/loop before adding its extension to importer/UI
filters. Do not advertise unsupported codecs based only on their extension.
