# Acceptance — 2026-11-05

## Scope and method
Native macOS arm64 GUI/CLI and the attached Adreno 840 were tested using the
user-provided three-frame capture. Source SHA-256:
`3c77f6fa01433cb46595109da7ccff5230634bc38c171cb72b52f78b4e49b1c4`.
Normal-operation measurements exclude GPU profiling/counters. The GUI harness
calls normal SelectExecution, includes debounce, replay, transfer, validation and
UI publication, alternates raw/LZ4, and uses five repetitions per draw/mode with
warm target-generated pipeline caches and cold local result caches. The 30
manifests assert no result-cache hit, target pipeline reuse and matching image/
buffer hashes. Values below are medians; small differences are not claimed as a
universal improvement.

## Replay-cache/batching improvements
Earlier GUI fresh selections of draws 194–196 took 3.18–3.61 s; the hash/cache/
batching changes reduced them to 1.75–2.13 s before production compression.
Retained draw revisits fell from roughly 1.06–1.16 s to 33–58 ms with zero ADB
calls. Cached CLI requests fell from 0.88–1.25 s to 0.38–0.54 s. Hash invalidation,
historical offline restores, forced-fresh misses, corruption and cancellation
are covered by regression tests.

## Production LZ4 round trips

| Draw | Raw GUI ms | LZ4 GUI ms | Raw download ms | LZ4 download ms | Raw/wire bytes with LZ4 |
| --- | ---: | ---: | ---: | ---: | --- |
| 194 | 1697.8 | 1684.1 | 177.3 | 189.4 | 1653984 / 755711 |
| 195 | 2072.5 | 1885.2 | 495.8 | 308.2 | 15734411 / 4363711 |
| 196 | 1990.4 | 1847.3 | 405.7 | 285.3 | 10509591 / 3211568 |

Draws 195 and 196 improved by about 9% and 7% end to end. Draw 194 was essentially
unchanged within observed variation. Separate fresh CLI draw-195 processes had
median total wall times of 2.759 s raw and 2.598 s LZ4 over three runs per mode.
First deployment is a separate one-time helper upload; warmed GUI requests reuse
the verified helper. All image and buffer hashes matched across modes/repeats.

## Integrity and compatibility
- Nine legacy framing cases, sixteen compressed envelope/cancellation cases and
  five deployment/fallback cases passed. They cover header/hash/length damage,
  frame corruption/truncation/concatenation, trailing data, invalid records,
  preservation of previous files, staging cleanup and unsupported helper ABI.
- A separately built diagnostic helper deliberately emitted the wrong leading
  checksum on the real phone. The CLI rejected replay with a checksum/size
  mismatch, published no readbacks or successful manifest and removed staging.
- A fresh draw from the final packaged CLI used the bundled helper, reported
  LZ4/XXH64, and matched raw reference image/buffer hashes. The installed replay
  APK was unchanged; helper deployment requires no APK replacement.
- Native ASTC 5.7.0 round trips passed for 4x4, 10x8 and 12x12 UNORM/SRGB data.

## Automated and distribution checks
- Full Release core and desktop GUI workflow, including Vulkan validation,
  passed (latest run: core 12.69 s, UI 43.37 s; zero failed tests).
- Settings tests exercised real user resizing, stable tab/section geometry,
  child scrolling, pinned tabs/footer, reopening, tool settings and shared device
  selection. GUI diagnostics reported no ImGui/ID/OpenGL errors.
- Python tests: 12 passed, one Windows-specific skip on this Mac.
- Extracted macOS package validation passed: inventory, ad-hoc signature,
  executable permissions, clean-PATH host/ASTC tools, shader round trips, samples,
  GUI/Launch Services startup, six local frame/full-draw replays and immutable
  package contents. Windows runtime acceptance was not rerun on this Mac.

## Evidence locations
Detailed local data remains under `.sessions/replay-probe-20261105/`:
`implemented-normal-ops-summary.json`, `lz4-production-gui.json`,
`lz4-production-cli.json`, `lz4-production-summary.json`,
`lz4-device-integrity.json` and `packaged-lz4-draw195.json`.
Logs remain in `build/lz4-integration-{core,workflow,python}.log` and
`build/macos-package.log`. A final release archive retains the ZIP, checksum,
package metadata, these summaries and logs outside Git. Captures and user state
are not embedded in this specification archive.
