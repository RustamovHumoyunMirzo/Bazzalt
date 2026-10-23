# Game build security protocol, version 3

This protocol provides authenticated content and optional confidentiality for an unchanged native player. It is not DRM or publisher identity. Keep the compiler and native implementations synchronized, bump the runtime manifest version on incompatible changes, and verify both Windows architectures in CI.

## Keys

The delivery key is 32 random bytes for embedded/file/DPAPI delivery, or the developer's 64-hex-character environment master. A fresh 32-byte build root, 16-byte build ID and 32-byte salt are generated for every encrypted build. HKDF-SHA256 (`info = ASCII "Bazzalt/v3/" + purpose`) derives 32-byte keys with purposes `wrapping`, `packages`, `configuration`, and `inventory`. The wrapping key derives from the delivery master; the other three derive from the fresh build root. The root is wrapped with AES-256-GCM, a fresh 12-byte nonce and build ID as AAD. Thus repeated use of an environment master never repeats an actual package data key.

Delivery options:

- `embedded`: the recoverable delivery master is in the executable footer, never the build root itself.
- `file`: `game.key` is the recoverable 32-byte delivery master. This option is not secrecy from anyone possessing the output.
- `environment`: the master is not shipped. Read only the configured signed environment name; missing/malformed/wrong keys fail closed without logging key values.
- `dpapi_user`: the delivery master is protected with Windows DPAPI current-user scope and the build ID as optional entropy. Only the protected blob is shipped, and the executable key field is zero. This is for private/local builds; it normally requires the building account on its machine and is not a portable retail mode. No machine-wide DPAPI flag or interactive prompt is used.

Reference: [Microsoft CryptProtectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata), [cryptography HKDF](https://cryptography.io/en/latest/hazmat/primitives/key-derivation-functions/#hkdf). The native RFC 5869 extract/expand implementation uses BCrypt HMAC-SHA256 and an independent compiler-generated vector test. It does not implement a new cipher.

## Manifests and signatures

`game.bootstrap` is bounded to 64 KiB. It includes format version, build ID, salt, delivery method/environment name, wrapped/protected key, encryption flags and expected player/seal hashes. Asset names and production/window settings are not in this bootstrap.

Encrypted `game.json` and `game.integrity` store `BZENC003` + nonce (12 bytes) + AES-GCM ciphertext/tag (16-byte tag). AAD is binary build ID + ASCII purpose (`configuration` or `inventory`). Manifests are decrypted in memory, not to plaintext manifest files beside the executable. With manifest encryption off, signatures still apply. The inventory contains output file paths/sizes/SHA-256 and full decoded content paths/sizes/SHA-256, with matching build ID.

Signature input is exact bytes: `BZSIG003` + little-endian u32 bootstrap byte length + bootstrap + stored inventory (ciphertext when encrypted). ECDSA-P256/SHA-256 signs that input; `game.signature` stores big-endian r (32) + s (32). Verify before parsing/decrypting bootstrap fields or trusting key delivery instructions. A one-build signing key is generated in memory and never shipped.

The 112-byte executable tail is `BZSIG003` + public X (32) + Y (32) + `BZKEY001` + embedded master/zeros (32). Authenticode's certificate table can follow this tail; the native reader accepts bounded alignment padding and obtains its location from the PE certificate directory rather than scanning the executable. The signed bootstrap hashes the complete tail and the executable prefix. Prefix hashing normalizes only the PE checksum and certificate-directory fields so signing does not break the check. The publisher must still sign and validate final binaries with a real certificate; the content signature does not certify its own trust anchor.

## Packages and stable reads

BPAK v3 retains the bounded compression/alias framing documented in `game-compilation.md`. For encrypted packages, the path field itself is nonce + AES-GCM ciphertext/tag with the record header as AAD. Payload AAD binds the header and the stored encrypted path. Segment digests use HMAC-SHA256 instead of revealing plaintext SHA-256 fingerprints; its key is `HMAC-SHA256(package-key, ASCII "Bazzalt/v3/segment-digest")`, separate from the AES key. Unencrypted records use SHA-256. File sizes, chunk counts and framing remain visible.

The signed inventory verifies complete package bytes before extraction. Decoded-file hashes are independently checked after decompression/alias resolution. Native file leases deny writes and deletion; directory leases deny replacement, and reparse points are rejected. Leases cover configuration, inventory, executable, runtime files, packages and extracted files until their consumers finish. Extracted leases are released before temporary-directory cleanup, including exception paths. The engine DLL remains delay-loaded until inventory verification. The player resolves its canonical executable path and restricts subsequent library searches to System32 and its explicitly registered runtime directory, excluding cwd and PATH search. OS/CRT loader code still executes before application checks.

Keys in the native reader are wiped during unwinding; DPAPI output is wiped before LocalFree. The compiler is a separate short-lived process; Python/crypto libraries may retain transient immutable copies, so complete memory zeroization is not promised. An authorized process must decrypt assets to use them, and an administrator can replace code or inspect memory. Do not describe these controls as absolute protection against the player.

## Tests

`BazzaltPackageSecurityTests` checks SHA-256/HKDF vectors and denied write/delete/directory replacement. Compiler tests cover envelope purpose/build separation, nonce/tag authentication, hidden paths, deduplication, key methods, wrong-key failure, modified configuration/bootstrap/inventory/signatures/packages/player bytes, decoded hash mismatches, Authenticode certificate-table layout, cancellation and actual native rendering. Real publisher-certificate enrollment and service integration need deployment-specific tests.
