## Context

The repository uses a shared C++ core, SDL3/ImGui GUI, JSON CLI and external
GFXReconstruct tools. macOS needs native process APIs, hashes and bundle paths.

## Decisions

- Use POSIX spawning without a shell, explicit argv/environment/cwd, bounded
  output and cancellation/timeout handling; use CommonCrypto for SHA-256.
- Use Contents/Resources for samples, tools, licenses and the generated ICNS.
  Keep GUI/CLI state under per-user Application Support, with --settings-dir
  available. This preserves the signed bundle after launch.
- Generate native icon sizes from assets/app-icon.png; retain one canonical
  macOS app/CLI distribution and platform-specific ZIP/checksum.
- Bundle only explicitly configured Android tools/APKs. Preserve bundled/PATH
  discovery and manual overrides; do not add SDK/JDK installation heuristics.
- Keep Vulkan runtime libraries external. Document explicit loader/MoltenVK
  configuration; rebind captured allocations for local macOS replay.
- Verify extracted artifacts outside the source tree with a clean PATH. Disable
  device discovery during package checks; run local replay only with an explicit
  loader path. Record hashes, source state, tool versions and signing status.
- Transfer the OpenGL context across serialized test-engine coroutine yields.
  Match macOS shortcut modifiers and reject missing frame-strip test references.
- Suppress background image focus while capture owns the completion modal.

## Limits

Packages are ad-hoc signed for local testing, not notarized. Runtime and GPU
capability limits remain explicit. Android runtime checks reused installed debug
APKs, not newly compiled Android outputs. Validation findings remain reported.
