## Context

Single draw replay already shares the GUI backend. Artifact metadata identifies outputs by submit, command, attachment and subresource. The backend can merge original readback selectors, but diagnostic configuration selects one pipeline and outline needs per-draw source images.

## Goals / Non-Goals

Goals: finish the five approved CLI improvements with additive JSON, useful errors and public-sample verification.
Non-goals: automatic asset naming, animation classification, changing captures or introducing a new export format.

## Decisions

- Keep `replay draw --visualize original|solid|wireframe|depth|outline|uv`. UV reflects the effective fragment shader; outline prepares original and solid coverage and composes a boundary. Unsupported interfaces fail explicitly.
- Add `--attachment color:N|depth|stencil` or `--image ID`, plus `--mip N` and `--layer N`. Selection is applied to output metadata only, never descriptor inputs. Nonzero subresources enable backend all-subresource readback. Unavailable selections return available output metadata. Default selection retains COLOR 0 preference. Outline source selection remains COLOR 0 regardless of the final selected output.
- Add `replay draws --executions LIST` accepting comma-separated indexes and inclusive ranges, bounded by `--batch-draws` (default 32). Import once, merge original selectors per frame, split artifacts by exact submit/command identity, and emit ordered per-execution results. Ambiguous repeated invocation selectors fail before replay. Diagnostic modes, subdraws and baseline comparison use isolated single-draw runs with explicit strategy and replay counts; this preserves correctness while avoiding static re-export.
- Geometry summaries use captured pipeline topology and replay arguments, with optional raw index readback. Count input-assembly triangles including instances and degenerate primitives, excluding restart markers. This is submitted geometry, not rasterized pixels or post-tessellation triangles. Unique vertices mean distinct adjusted vertex IDs across active records, independent of instance count. Dynamic topology/restart and unsupported commands report unavailable rather than guessing. Buffer availability scans referenced metadata, including missing files.
- Return `operation_timings` with total wall seconds, replay wall seconds, remaining frontend seconds and replay call count. Include auxiliary outline and baseline calls. Keep backend `timings` and `inspection_seconds` compatible. Measurements exclude process startup and final result serialization; cache hits remain visible in each run.

## Risks / Trade-offs

- All-subresource readback costs memory → enable only for nonzero mip/layer requests and retain bounded batches.
- Raw indices may be absent or huge → report unavailable with a reason and enforce a bounded scan.
- Diagnostics cannot share one backend configuration across arbitrary pipelines → disclose isolated strategy instead of incorrectly merging them.
- A batch can fail after earlier chunks complete → persist completed chunks and structured failures; do not claim all draws succeeded.

## Migration Plan

Existing commands and JSON fields remain valid. Build, run core and GUI checks, validate the package in a staging directory, then update shipped files while preserving sessions and settings. Rollback uses the prior local commit and delivery backup.
