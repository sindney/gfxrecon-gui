## Why

Preparing every diagnostic view makes selecting draws slow, especially on Android. Users need a fast default and an explicit option to preload all views.

## What Changes

- Add a persisted Settings choice: Light (default) or Full draw replay.
- Light loads only the requested view and its dependencies, retaining views already fetched.
- Full retains eager preparation of all six views.
- Keep resource inspection and shader editing available with either policy.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `draw-diagnostics`: Policy-controlled preparation, incremental caching and latest-selection safety.

## Impact

Settings dialog, workspace preferences and replay scheduling, GUI tests and guide. Changes remain uncommitted pending review.
