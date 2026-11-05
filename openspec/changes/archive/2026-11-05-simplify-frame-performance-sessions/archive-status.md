# Final profiling design archived — 2026-11-05

The user approved archiving all eight related changes and syncing only the final implemented specifications. Current specs retain the delivered capture relations, GPU timestamp, swapchain, memory policy, optional counter, Perf UI and failure-artifact behavior. Obsolete restoration/profile deltas remain historical in their respective archives.

The later minor UI refinements are reflected in the current specs: all-counter capture, optional Probe, docked Perf, Mean/Min/Max with readable units, one-click additive row toggles, Shift ranges, filtered Select/Deselect all, a fixed-width placeholder-only filter, unlimited graphs and horizontal-only time zoom. Display filtering does not affect acquisition or saved evidence.

Final cleanup also removed two unused one-off test commands and scratch/build artifacts, while preserving capture evidence. Full core/CLI checks, ten packaging tests, and seven Perf GUI cases on each of SDL/SFML passed. Windows transient atomic JSON replacement failures now use a bounded retry that preserves the previous artifact on persistent denial; capture journals share the same helper. No source tests were skipped to obtain these results.
