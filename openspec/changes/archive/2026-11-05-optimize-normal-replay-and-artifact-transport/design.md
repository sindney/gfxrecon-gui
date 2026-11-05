# Final implementation

## Normal replay
A process-local 512-entry hash cache keys canonical paths by file identity,
size, modification time and change time. Changed stamps force hashing again;
cancellation is checked on hits as well as reads. Completed GUI draw snapshots
retain shaders, mesh data, resource references and fetched diagnostic modes in an
LRU bounded to 16 selections and 128 MiB of resource-file working set. Capture,
selection/subdraw, target, overrides and replay-policy identities govern reuse.

Ordinary Android disk results can be hash-verified before ADB access when the
serial is explicit. Stored device provenance is historical: the new manifest
sets device_identity_source=cached_result and current_device_verified=false.
Fresh, validation and measurement requests bypass this result reuse.

## Transfer protocol
One run-as producer emits NUL-framed resource names, lengths and binary payloads.
The shell-owned ARM64 helper reads that stream, computes XXH64 while compressing
it into an immediately unlinked bounded-memory spool, then writes:

`GFXRECON_TRANSFER_1\0lz4\0raw_bytes\0payload_bytes\0xxh64_hex\0<LZ4 frame>`

The checksum covers the complete uncompressed artifact stream, including names,
lengths and its terminator. LZ4 block/frame checksums are also enabled. The host
requires exact wire/decompressed lengths, one completed frame, the content hash,
and valid bounded artifact records. All resources remain in an owned staging
folder until these checks pass. Failed or cancelled transfers cannot become a
successful result; a content-check failure never silently retries as raw.
XXH64 provides transfer integrity, not peer authentication. Independent SHA-256
identifies the helper binary, capture, backend and persisted artifacts.

The helper is uploaded atomically under its SHA-256 filename in /data/local/tmp,
verified, and execution-probed for the supported ABI. Verified deployments are
reused within the frontend process. An absent, unsupported or unavailable helper
uses the existing raw protocol. APK behavior and installation remain unchanged.

## Workspace and distribution
Global preferences and tool/recovery controls live in Settings. Initial geometry
is bounded by the viewport and retained rather than auto-sized; users can resize
and move it. A scrolling child contains tab content, leaving tab headers and
Close visible. Events owns one shared Automatic/local/Android selector and
persists explicit serial selection; target changes invalidate derived views.
Perf uses the same target and retains its existing measurement controls.

macOS builds acquire the pinned universal ASTC release with archive verification;
packaging checks its clean-PATH encode/decode and bundled helper capability/hash.
Replaced OpenGL texture IDs are deleted only after current ImGui draw submission.

## Acceptance limits
The device benchmark isolates normal replay with warm target PSOs and cold
result caches. Large readbacks improved end to end; the smaller draw was within
normal timing variation. This is not a GPU-duration or universal-speedup claim.
Windows packaging contracts are covered by Python tests; a Windows runtime build
was not executed on this Mac.
