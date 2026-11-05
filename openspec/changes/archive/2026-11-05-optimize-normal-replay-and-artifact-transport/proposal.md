# Optimize normal replay and verify Android artifact transport

## Why
Ordinary draw selection and CLI replay repeatedly hashed unchanged captures,
queried the phone even for local cache hits, and downloaded each resource
separately. Large fresh readbacks still spent measurable time in USB transfer.
Tool/device controls were scattered, Settings resized with its content, and
macOS ASTC discovery and texture replacement needed repair.

## What Changes
- Retain bounded completed draw inspections and validated file hashes; restore
  compatible historical Android results before contacting an explicitly selected phone.
- Batch replay metadata/setup and resource downloads. Add a separately deployed
  ARM64 LZ4 helper with an upfront XXH64 content hash and lengths, private staging,
  strict completion checks, cancellation and optional raw fallback.
- Put tool/global replay configuration in a resizable, stable-size Settings
  dialog. Select the shared replay target through an Events phone button beside
  an icon-only Perf clock; remove the Perf phone label.
- Discover and package pinned native macOS ASTC tooling, retain bundled ADB,
  and retire replaced OpenGL textures after queued ImGui draws are submitted.

## Capabilities
### New Capabilities
- `normal-replay-cache`: bounded memory/hash reuse and honest historical disk-result provenance.
- `android-artifact-transport`: compressed, checksum-first Android downloads and helper deployment.
### Modified Capabilities
- `inspection-workspace`: Settings, replay-device controls and safe texture retirement.
- `performance-dialog`: icon-only launcher and one shared target selector.
- `macos-distribution`: native ASTC and configured artifact-helper packaging.
- `agent-replay`: identify historical cache provenance without claiming current-device verification.

## Impact
Shared replay/core, GUI, CLI, Android helper source/build script, packaging,
regression tests and current specs change. The compatible replay APK is unchanged.
Raw capture data, machine settings, diagnostic probes and distribution binaries
remain local artifacts. Acceptance is recorded in validation.md.
