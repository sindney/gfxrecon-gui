# Archive disposition — 2026-10-29

Archived at the user's request after visual review, device verification and local
release publication. This archive is a checkpoint, not a claim that every
original acceptance item has passed. There are 120 completed tasks and five
unchecked follow-ups. Capability specs describe the intended baseline; the
acceptance limits below still apply.

## Reconciled historical checkboxes

Seven old task markers were stale relative to the later implementation:

| Task | Existing implementation and acceptance evidence |
| --- | --- |
| 5.5 | Recorded pipeline/stage/entry/specialization state, descriptors, dynamic state, targets and buffer bindings: `src/analysis/state.cpp`, `src/tests/analysis_tests.cpp`, `docs/CLI.md`. |
| 6.4 | Image uploads/thumbnails, display controls, pixel inspection, raw/scalar buffers and vertex/index tables: `src/gui/`, `src/tests/mesh_tests.cpp`, `docs/CLI.md`, native/UI/portable regression. |
| 6.5 | Known upstream Windows and original-phone screenshot/resource checks: `docs/CLI.md`, subsequent real-game resource/readback checks in `docs/CLI.md` and `docs/CLI.md`. Unsupported formats/paths remain explicit. |
| 7.2 | Exact shader extraction, module IDs independent of binary hashes, lifetime/pipeline references, stage/entry/specialization metadata: `src/analysis/`, shader services and identity fixtures; `docs/CLI.md`, `docs/CLI.md`. |
| 10.1 | Deterministic native tests cover parsing/truncation, cache integrity, execution state, transport/recovery, shader diagnostics and failed-result retention; hardware checks remain opt-in. See `src/tests/` and the latest native/UI test records. |
| 10.2 | ImGui coverage exercises import/navigation, linked inspectors, cancellation/errors, editor compile/apply/compare/revert, persisted layouts and Frames activation. Latest full regression: `build/unlock-workflow-tests.log`, 79.39 seconds, zero ImGui errors or ID conflicts. |
| Validation target | Authorized Android device or local Vulkan device | Selected at runtime |

## Unfinished work retained in tasks.md

- **7.4:** Complete broader shader extraction/cross-compilation and failure-path acceptance across known stage/lifetime/debug/specialization cases. Existing named-entry and interface tests are partial evidence.
- **8.6:** Complete GPU compute-output edit/compare/revert acceptance and the remaining shared-module/race coverage. Fragment-color and named-entry game edits passed; compute compilation/interface checks alone are insufficient.
- **8.7:** Exercise advanced inline/library/shader-object replacement identities before enabling them. Current unverified paths remain disabled with diagnostics.
- **10.5:** Finish the capability-by-capability acceptance audit after the remaining items have evidence. Workflow documentation exists; the entire original spec is not declared fully accepted.
- **14.5:** Perform Android delayed N-frame/manual-stop capture acceptance, continued game lifetime, retrieval and exact settings restoration. Windows on-demand and earlier Android scheduled capture checks do not replace this test.

## Published checkpoint

Canonical runtime: `dist/GfxReconGui/GfxReconGui.exe`. The extracted ZIP passed
startup and native/UI/replay workflows with zero ImGui errors and conflicting-ID
Use the bundled triangle or indexed-cube capture for reproducible validation. Keep private captures and machine-specific measurements outside the repository.
verified after restoring 3,044 runtime/user files. The installed Android replay
APK matches the bundled APK byte for byte. Release/session data, local tool
configuration and authored edits remain outside Git.

Final evidence: `build/unlock-package.log`, `build/packaged-workflow.log`,
`build/unlock-restore.log`, `.sessions/unlock-overlay-review/game-acceptance/`
and `.sessions/dist-migration/1793255138104155900/plan.json`.
