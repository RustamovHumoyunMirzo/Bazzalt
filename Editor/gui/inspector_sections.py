"""Presentation-only grouping. Property identifiers and serialization stay stable."""
GROUPS = {
    "Camera": {
        "Projection": ("Projection", "Field of View", "Orthographic Size", "Near", "Far"),
        "Output": ("Render Target", "Viewport", "Aspect Ratio", "Aspect Mode", "Priority", "Active", "Clear Color"),
        "Post Processing": ("Post Processing", "Bloom", "Ambient Occlusion", "Anti Aliasing", "Tone Mapping", "Exposure", "Post Process Materials"),
        "Post Processing/Depth of Field": ("Depth of Field", "Focus Distance", "Aperture", "Shutter Speed", "Sensitivity"),
    },
    "Light": {
        "Shape": ("Range", "Inner Cone", "Outer Cone", "Sun Angular Radius", "Sun Halo Size", "Sun Halo Falloff"),
        "Shadows": ("Cast Shadows", "Shadow Far", "Shadow Bias", "Shadow Normal Bias"),
    },
    "AudioSource": {
        "Playback": ("Clip", "Volume", "Pitch", "Loop", "Play On Awake", "Priority", "Output Bus"),
        "Spatial Audio": ("Spatial Blend", "Min Distance", "Max Distance", "Rolloff Curve", "Custom Rolloff", "Doppler Level", "Cone Inner Angle", "Cone Outer Angle", "Cone Outer Gain"),
    },
    "Rigidbody": {
        "Dynamics": ("Mass", "Gravity Factor", "Linear Damping", "Angular Damping", "Max Linear Velocity", "Max Angular Velocity"),
        "Collision": ("Friction", "Restitution", "Collision Layer", "Motion Quality", "Sensor"),
    },
}


def FieldGroup(component, name):
    key=lambda value:value.replace(" ", "").casefold()
    return next((group for group, fields in GROUPS.get(component.replace(" ", ""), {}).items() if key(name) in {key(field) for field in fields}), "")
