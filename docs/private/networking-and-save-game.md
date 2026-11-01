# Networking and save-game implementation

The core owns service configuration, application identity, owner-thread checks and shutdown.
Yojimbo is privately compiled into bazzalt_network. It does not link core and communicates
through the versioned NetworkModuleApi dispatch table loaded only beside core.
Core and transport share the small bazzalt_crypto sodium provider; never package one without it.
Engine Update/RenderEditorFrame pump transports on real wall time and start FIFO save IO jobs.
Services survive scene transitions; EndApplication flushes saves after user shutdown callbacks
and closes endpoints. Editor and player use distinct SDL preference subdirectories.

Persistence container v1: little-endian BZSAVE01 magic, format UInt16, length-prefixed
bounded metadata, length-prefixed encoded/encrypted payload, CRC32 of preceding bytes.
Metadata includes encryption flags, schema, generation, timestamp, title, application binding
and nonce; all is AEAD additional authenticated data when encryption is enabled.
Document v1 uses deterministic typed recursive encoding; never serialize raw C++ layouts.
OS locks coordinate slot writes, native durable temp writes precede atomic replacement.
Migration callbacks never execute on IO workers. Tests use private SaveAccess overrides,
not an exposed directory setter. Do not expose runtime headers in either scripting API.

Run BazzaltNetworkSaveTests for real localhost UDP and disk transaction tests.
Source-only CI bootstraps pinned Yojimbo using scripts/get_yojimbo.*; no large dependency
rebuild is required to develop these modules. Distribution scripts must include both
modules, matching architectures, and all yojimbo-* notices.
