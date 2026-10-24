# Application events

The current implementation is documented in [global-subsystems.md](global-subsystems.md).
Global listeners belong to registered C++ Subsystem Scripts. Ordinary C++/Lua
entity scripts use Application controls, not application event registration.
The previous Application-derived script and Lua bootstrap scheme was replaced.
