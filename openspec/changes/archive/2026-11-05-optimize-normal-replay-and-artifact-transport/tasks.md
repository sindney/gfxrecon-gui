## 1. Normal replay caching and batching
- [x] 1.1 Add bounded file-hash memoization with metadata invalidation and cancellation checks.
- [x] 1.2 Retain completed draw inspections with policy/generation checks and bounded eviction.
- [x] 1.3 Resolve compatible historical Android disk results before device access; retain honest provenance and fresh bypasses.
- [x] 1.4 Batch metadata, setup and artifact downloads while retaining upload checksum acknowledgment.

## 2. Verified LZ4 transport
- [x] 2.1 Build/package the separately deployed ARM64 helper and bind capabilities/deployment to its SHA-256.
- [x] 2.2 Send content hash and lengths before the compressed payload; verify complete frame/content/records before publication.
- [x] 2.3 Preserve cancellation, staging cleanup and raw fallback for an unavailable helper.
- [x] 2.4 Cover corrupt, truncated, trailing, wrong-length/hash, cancelled and fallback cases, including a real-device checksum fault.
- [x] 2.5 Measure alternating raw/LZ4 normal GUI selections and CLI requests on the attached device with identical readbacks.

## 3. Workspace and macOS fixes
- [x] 3.1 Centralize tool/replay configuration in Settings and add shared Events device selection; simplify Perf icons.
- [x] 3.2 Keep Settings geometry stable, resizable and retained; scroll content with pinned tabs/footer.
- [x] 3.3 Package/discover native ASTC and verify it under a clean PATH; retain configured bundled ADB.
- [x] 3.4 Defer texture retirement until submitted UI draws have finished using old IDs.

## 4. Validation and archival
- [x] 4.1 Pass core, full desktop GUI/validation, Python packaging and extracted macOS package checks.
- [x] 4.2 Verify a fresh packaged Android draw with the bundled helper and matching resource hashes.
- [x] 4.3 Record final behavior and measured results in the archive and synchronize current specifications.
