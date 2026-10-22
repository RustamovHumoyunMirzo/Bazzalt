# Transform gestures and wheel editing

Scene free move has the same immutable-start camera-plane solver as translate
gizmos, but activates from the selected object's actual hit rather than an
axis handle. Handle hit-testing runs first; Select mode and Ctrl-selection
retain marquee behavior. A four-pixel threshold prevents clicks turning into
transforms. Translate/rotate/scale modes all allow free translation of selected
objects. The effective operation is the drag's Mode, not the toolbar mode.
History begins once on threshold crossing and commits once on release/focus
loss. Selected descendants are not translated a second time if an ancestor is
also selected.

`transform_trails.py` produces finite, bounded colored line segments: a starting
cross and sampled movement path, an angular arc, or reference/scaled boxes with
connecting edges. Geometry uses a camera-relative initial size, not a fixed
world-size marker. Translate paths retain their initial point and at most 256
samples; the native bridge rejects more than 2048 segments and nonfinite values.
The private Engine appends these to Scene-only guides. Game views, public APIs
and serialized scenes do not contain this editor geometry. Selection clearing,
scene switching and viewport shutdown clear it. Preferences independently toggle
each operation and control post-release persistence from 0 to 10 seconds.

`EditorWheelPolicy` is installed once per editor window and removed on close.
It covers shared and directly constructed spin boxes, combo boxes and sliders,
including their focused child editors and editor-owned modal/floating widgets.
Value controls use StrongFocus rather than wheel focus. Unfocused wheel events
are forwarded to the nearest scroll area's viewport, while scroll bars and
the native Scene surface retain their own wheel behavior. The forwarding guard
prevents recursion; focused value fields keep normal wheel adjustment.
