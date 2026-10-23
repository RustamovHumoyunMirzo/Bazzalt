# Mixer integration and DSP

`Editor/gui/panels/mixer_controls.py` adapts the project author's testamixer.py;
the original root file is left untouched as a reference. The demo palette,
demo signal generator, JSON dialogs and standalone entry point are not used.
Shared theme rules live in Editor/theme.py. Menus use EditorMenu. Locale keys
are mapped by mixer_locale.py, without translating persistent effect type or
parameter identifiers. The panel wrapper owns project selection and IO.
The rack/sidebar controls are built lazily on asset open or first visible use;
dock registration must not reparent and restyle a hidden, unused mixer tree.

Bus IDs are UUIDs independent of group names. Effect IDs are stable UUIDs;
the adapter maps its internal effect handles to them. Mixer YAML Version 2
adds Bypass and an ordered Effects sequence per bus. Version 1 stays readable.
EditorState is a quoted JSON scalar within YAML for the adapted model's
snapshots, views, colors and lock/visibility preferences, including UUID maps.
Core playback reads Buses/Effects and does not depend on Qt or editor metadata.
Saving through a legacy bus-only caller preserves existing EditorState.

Each miniaudio sound group feeds a custom BusProcessor node, then its parent
sound group (Master feeds the endpoint). The processor applies effects in order
and atomically accumulates stereo peaks. The callback has preallocated delay
buffers and owns its DSP histories; no allocation, UI calls or mutex occur in
the callback. ma_node_uninit synchronizes teardown. Graph edits occur on the
control thread. Gain-only edits reuse processors; effect configuration changes
replace histories without recreating voices. The bridge caches meter polls for
33 ms so panel and inspector don't consume each other's measurements.

Low/high-pass and parametric EQ use normalized biquad coefficients. Echo uses
a stereo feedback ring with feedback capped to .999. Distortion blends soft
tanh saturation (zero Level is transparent). Reverb is a compact four-tap
decorrelated feedback room, not convolution or an external reverb preset.
All effects run at the engine's 48 kHz stereo mix rate. Validation limits total
effect count and aggregate delay storage before allocating processors.

Properties contains audition controls, not authoring controls. Preview loads
the selected mixer only outside gameplay, routes the chosen clip into a bus,
and uses native AudioVoice transport. Mixer saves update stats without calling
SelectAsset again (which would stop playback and rebuild the inspector).
AssetBrowser.Refresh blocks transient selection signals and restores all
selected paths and scroll position, preventing file-watcher deselection.

The inspector waveform is source PCM, not synthetic signal or post-effect
metering. The private module-level audio_waveform function decodes independently
of EditorHost/Engine, with the GIL released. The Qt-side bounded worker pool
must capture only that function and a cache-file path: retaining an EditorHost
in a worker could move Filament destruction onto the worker on shutdown.
512 peak envelopes span the entire clip; each bin reads at most 32768 stereo
frames, using seeks for longer bins. Stereo channels contribute independently
to avoid cancellation of opposite-phase samples. Results are cached by file
path, size and timestamp; QTimer delivers completions on the GUI thread.

Extend an effect in all layers: AudioEffectType/Settings, native validation,
AudioEffects.h, YAML reader/writer, Python bridge, mixer EFFECT_LIBRARY,
LuaAudio bindings, PCM tests and both public API references. Never populate
meters with synthetic levels or accept unknown effect parameters silently.
