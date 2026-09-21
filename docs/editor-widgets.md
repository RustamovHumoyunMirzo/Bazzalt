# Editor field widgets

`Editor.gui.widgets` contains reusable PySide controls for Properties and other editor tools.
They deliberately operate on ordinary Python values and UUID-like objects so a future
component-field/reflection layer can bind them without coupling UI code to runtime ECS types.

## Typed inputs

- `StringInput`, `BoolInput`, `IntInput`, and `FloatInput` expose `GetValue()` and
  `SetValue(value)` while retaining their standard Qt change signals.
- `Vec2Input`, `Vec3Input`, and `Vec4Input` group labelled floating-point axes and emit
  `ValueChanged(tuple)`. `SetRange(minimum, maximum)` applies constraints to every axis.
- `RangeInput` synchronizes a float input with a high-resolution slider. Values are clamped to
  the range supplied at construction and changes emit `ValueChanged(float)`.
- `EnumInput.SetOptions((label, value), ...)` stores display labels separately from serialized
  values. `SetValue` returns false when the value is unavailable.
- `MultiSelectInput` displays a checkable popup and exposes ordered selected values through
  `GetValues()` and `SelectionChanged(tuple)`.
- `ColorInput` displays an ARGB swatch and opens the native color picker.

## Object and asset pickers

`ObjectPickerInput` and `AssetPickerInput` derive from `PickerInput`. The display name is kept
separate from the underlying value, which may be an ECS entity UUID, asset UUID, or descriptor.
Connect `PickRequested` to the appropriate editor browser, then call
`SetValue(value, display_name)` with the result. `Clear`, `Cleared`, and `ValueChanged` provide
the normal nullable-reference workflow.

## Field state

Inputs implement `SetFieldState(FieldState)` and `SetReadOnly(bool)`. States are `Default`,
`Modified`, `Warning`, and `Error`; their colors come from the active application theme. This
allows validation and prefab/override visualization without per-component stylesheets.

The theme now provides semantic colors for hover, pressed, input background, focus, selection,
disabled text, success, warning, error, modified values, and X/Y/Z/W axes. Both built-in dark
and light themes define every token.
