# Game build limitation register

Scope: the game compiler, packaging, native Windows player and editor Build workflow. This is not a claim that the entire engine is free of other limitations.

## Addressed in this audit

- Current-generation import cooking instead of recursively copying old cache generations.
- Disposable build workspaces, bounded ZIP launch copies and recovery of explicitly owned abandoned workspaces.
- BPAK v3 compression before encryption, bounded decompression, encrypted asset paths and full-file payload deduplication.
- Signed complete output inventory, pinned per-build verification key, configuration/package/library validation and delayed engine loading.
- Private extraction-directory ACLs, active-session locking and subsequent-launch recovery of marked crash leftovers.
- Strict paths, chunk framing and duplicate handling; encryption-key buffer wiping during exception unwinding.
- Security profiles do not add random padding automatically or pretend chunk size increases cryptographic strength.
- AES-GCM configuration/inventory encryption, per-build wrapped data keys and domain-separated key derivation.
- Optional Windows account-bound key protection, executable/seal hashing, decoded-file hashes and lifetime read-only file leases.
- Opt-in asset inclusion/exclusion rules with serialized dependency closure; cooperative build cancellation before publication.

## Remaining implementation limitations

| Area | Current behavior and why it remains | Next change |
| --- | --- | --- |
| Virtual filesystem | Entire package is verified and extracted before play. Files are plaintext during play, consume decoded disk space, and add startup I/O. The engine, third-party loaders and native module loader currently consume filesystem paths. | Introduce an asset byte/stream provider across every runtime consumer; native DLLs still require a separate loading strategy. Do not implement a fake in-memory archive while silently extracting it anyway. |
| Crash residue | Marked leftovers are reclaimed on a subsequent launch, not immediately after a crash or power loss. Older unmarked leftovers are preserved. Secure deletion is not guaranteed on SSDs, journaling filesystems or backups. | Optional external cleanup service/session manager; explicit user cache cleanup UI for legacy directories. |
| Asset stripping | Opt-in patterns and serialized dependencies are supported. Automatic discovery of assets requested dynamically by C++/Lua is not possible in general. Import/validation still precedes output filtering. | Keep dynamic loads explicitly; extend dependency providers for additional serialized formats and filter before import when their identities are known. |
| Source/import duplication | Byte-identical copies are aliases in BPAK, but differing authoring/runtime products still ship where path-based consumers need them. Loose output and extracted output retain decoded copies. | Consumer-specific runtime product manifests and the VFS migration. |
| Incremental distribution | No content-addressed patch manifest, delta patcher or updater. Cooking/packaging still visits all included files; archives are whole-build deliverables. | Versioned patch protocol with signed update metadata and rollback policy. |
| Build cancellation | Cooperative cancellation now preserves prior output and cleans temporary work. It waits for the current compiler/cooker/shader tool to finish; a hung tool can still delay it indefinitely. | Process-job isolation with reliable descendant termination and rollback tests before offering immediate abort. |
| Platform | Production compiler/player integration targets Windows x86/x64; editor and compiler host are x64. Compression uses Windows Cabinet. Local verification covers x64; x86 stays in CI. | Separate Linux/macOS native player/codec/platform packaging and CI. |
| Windowing | Fullscreen is desktop-borderless on the primary monitor, not exclusive display-mode switching; there is no monitor/resolution selector. | Display enumeration and validated output-mode settings. |
| Limits | Reader deliberately caps segment sizes, path lengths, records and decoded bytes. These are denial-of-service guards, not bugs to remove. | Revisit only with memory/disk budgets and adversarial tests. |

## Requires project/deployment authority

- **Publisher identity / Authenticode:** content signatures pin to the shipped executable, not a certificate authority. A modified executable can replace the verification key or bypass checks. A trusted certificate, signing service or hardware-backed signer must be supplied by the publisher. The compiler does not invent a publisher identity. OS/CRT dependencies may execute before application-level verification; authenticate executable/DLL distribution using OS code-signing policy.
- **Authenticode deployment verification:** the player locates its footer before a PE certificate table, including alignment padding, instead of assuming executable EOF. Tests cover that layout, but no publisher certificate was available for a real signing-service test. Verify the exact signed output with the real certificate/toolchain before distribution.
- **Online key delivery:** environment keys must be provisioned out of band. There is no server, authentication service, entitlement protocol, revocation or secure enrollment supplied by this project.
- **DRM / anti-cheat / script sandbox:** not implemented. Native game scripts are trusted native code, not untrusted user plugins. Lua bindings expose game APIs and are not an OS security boundary.
- **Key enrollment:** DPAPI user protection is local/account-bound, not a key distribution service. Portable stronger enrollment requires recipient keys, a signing/key service or publisher provisioning. Environment master keys are reused only as wrapping roots; each build's actual data key is fresh.

## Fundamental client-side limits

- A player controlling the machine/process can inspect decrypted memory, capture rendered/audio output, patch native code and recover client-side keys. Encryption is useful against offline/casual extraction, not absolute secrecy from the player.
- Embedded and sibling key files are deliberately recoverable delivery methods. An environment key avoids shipping that key with the package but is still available to an authorized running process.
- Random padding, smaller chunks and obfuscation cannot become authentication or cryptographic security. High and Very high currently use the same AES-256-GCM and content-signature algorithms.
- Verified output and extracted files now have lifetime read-only leases; directory replacement and reparse points are rejected. These checks are not an OS sandbox, do not authenticate arbitrary files opened by trusted native game code, and cannot stop an administrator patching the player or process memory.
- Minimal bootstrap flags, random identifiers, package counts and sizes remain visible. Encrypted manifests and record paths hide asset names, not the existence/size of the installed game. The separate Python compiler may retain transient secret copies; no complete memory-erasure guarantee is made.
