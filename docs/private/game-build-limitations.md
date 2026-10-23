# Game build limitation register

Scope: the game compiler, packaging, native Windows player and editor Build workflow. This is not a claim that the entire engine is free of other limitations.

## Addressed in this audit

- Current-generation import cooking instead of recursively copying old cache generations.
- Disposable build workspaces, bounded ZIP launch copies and recovery of explicitly owned abandoned workspaces.
- BPAK v2 compression before encryption, bounded decompression and full-file payload deduplication.
- Signed complete output inventory, pinned per-build verification key, configuration/package/library validation and delayed engine loading.
- Private extraction-directory ACLs, active-session locking and subsequent-launch recovery of marked crash leftovers.
- Strict paths, chunk framing and duplicate handling; encryption-key buffer wiping during exception unwinding.
- Security profiles do not add random padding automatically or pretend chunk size increases cryptographic strength.

## Remaining implementation limitations

| Area | Current behavior and why it remains | Next change |
| --- | --- | --- |
| Virtual filesystem | Entire package is verified and extracted before play. Files are plaintext during play, consume decoded disk space, and add startup I/O. The engine, third-party loaders and native module loader currently consume filesystem paths. | Introduce an asset byte/stream provider across every runtime consumer; native DLLs still require a separate loading strategy. Do not implement a fake in-memory archive while silently extracting it anyway. |
| Crash residue | Marked leftovers are reclaimed on a subsequent launch, not immediately after a crash or power loss. Older unmarked leftovers are preserved. Secure deletion is not guaranteed on SSDs, journaling filesystems or backups. | Optional external cleanup service/session manager; explicit user cache cleanup UI for legacy directories. |
| Asset stripping | All indexed assets and scenes ship. Runtime C++/Lua can request any UUID/path dynamically, so automatically guessing reachability would break games. | Explicit build roots, scene list, keep rules and dependency tracking before enabling opt-in stripping. |
| Source/import duplication | Byte-identical copies are aliases in BPAK, but differing authoring/runtime products still ship where path-based consumers need them. Loose output and extracted output retain decoded copies. | Consumer-specific runtime product manifests and the VFS migration. |
| Incremental distribution | No content-addressed patch manifest, delta patcher or updater. Cooking/packaging still visits all included files; archives are whole-build deliverables. | Versioned patch protocol with signed update metadata and rollback policy. |
| Build cancellation | Editor's build modal blocks close while a build runs; it does not yet offer cooperative cancellation of cooker/subprocess trees. | Cancellation protocol and process-job cleanup with transactional publication tests. |
| Platform | Production compiler/player integration targets Windows x86/x64; editor and compiler host are x64. Compression uses Windows Cabinet. Local verification covers x64; x86 stays in CI. | Separate Linux/macOS native player/codec/platform packaging and CI. |
| Windowing | Fullscreen is desktop-borderless on the primary monitor, not exclusive display-mode switching; there is no monitor/resolution selector. | Display enumeration and validated output-mode settings. |
| Limits | Reader deliberately caps segment sizes, path lengths, records and decoded bytes. These are denial-of-service guards, not bugs to remove. | Revisit only with memory/disk budgets and adversarial tests. |

## Requires project/deployment authority

- **Publisher identity / Authenticode:** content signatures pin to the shipped executable, not a certificate authority. A modified executable can replace the verification key or bypass checks. A trusted certificate, signing service or hardware-backed signer must be supplied by the publisher. The compiler does not invent a publisher identity. OS/CRT dependencies may execute before application-level verification; authenticate executable/DLL distribution using OS code-signing policy.
- **Authenticode deployment verification:** the player locates its footer before a PE certificate table, including alignment padding, instead of assuming executable EOF. Tests cover that layout, but no publisher certificate was available for a real signing-service test. Verify the exact signed output with the real certificate/toolchain before distribution.
- **Online key delivery:** environment keys must be provisioned out of band. There is no server, authentication service, entitlement protocol, revocation or secure enrollment supplied by this project.
- **DRM / anti-cheat / script sandbox:** not implemented. Native game scripts are trusted native code, not untrusted user plugins. Lua bindings expose game APIs and are not an OS security boundary.

## Fundamental client-side limits

- A player controlling the machine/process can inspect decrypted memory, capture rendered/audio output, patch native code and recover client-side keys. Encryption is useful against offline/casual extraction, not absolute secrecy from the player.
- Embedded and sibling key files are deliberately recoverable delivery methods. An environment key avoids shipping that key with the package but is still available to an authorized running process.
- Random padding, smaller chunks and obfuscation cannot become authentication or cryptographic security. High and Very high currently use the same AES-256-GCM and content-signature algorithms.
- Signed manifests cannot stop filesystem changes by a hostile process after verification. Fully addressing that threat requires authenticated reads/held handles, platform execution policy and a coherent trust model; it cannot be guaranteed against an administrator patching the player.
