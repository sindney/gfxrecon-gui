## Context

A draw fetch currently loads shaders, inputs and mesh data, then replays every diagnostic mode before showing the result. Its cache already identifies the selected execution and indirect record.

## Goals / Non-Goals

Goals: default to responsive on-demand views, preserve fetched views, and retain optional full preloading.
Non-goals: removing resource readbacks, changing CLI detail flags, or changing shader override scope.

## Decisions

- Persist `full_draw_replay` in workspace preferences, defaulting to false when absent. Expose Light (on demand) and Full (all views) in Settings.
- Snapshot the policy and requested mode when scheduling. Light requests only the selected mode; outline also needs original and solid coverage. Generate UV shaders only when UV is requested or Full is selected.
- Reuse the original readback and cached diagnostics when execution, generation and indirect record match. Merge results without losing earlier modes. Reusing a mode launches no replay.
- Mode changes during a running query update the desired mode. If the completed cache lacks that mode, schedule its missing passes next. A different execution/record still cancels obsolete work using generation checks.
- Preserve explicit unsupported diagnostics; failed modes do not discard working cached views. Settings changes affect subsequent queries, without launching work from the dialog.

## Risks / Trade-offs

- Outline still needs more than one pass → fetch only missing prerequisites.
- A mode can change mid-query → after completion, queue only the latest uncached request.
- Stale inputs could cross selections → only reuse the matching generation, execution and record.
- Existing preferences omit the key → use Light for both new and upgraded installations.
