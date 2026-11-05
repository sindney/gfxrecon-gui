## ADDED Requirements

### Requirement: CLI sustained GPU and counter sessions

`gfxrecon-cli perf` SHALL expose an explicit sustained mode for one selected frame with warm-up, measured duration, pacing and required counter selection. It SHALL use the shared performance-session core, default sustained swapchain policy to offscreen, preserve existing manual timing behavior, and record actual effective policy. With overrides it SHALL retain baseline/modified alternating session order and individual iteration/counter evidence. The initial delivery SHALL make the CLI contract testable before GUI integration.

#### Scenario: Precision profile from a short capture
- **WHEN** a user requests a sustained session for frame N with the precision counter profile
- **THEN** the CLI checks provider/restoration support, runs configured warm-up and measured repetitions, and returns GPU timing plus required counter results or an explicit incomplete/unsupported failure

### Requirement: Sustained result artifacts and comparisons

The CLI SHALL return a versioned result with phase/iteration timing, descriptors and timestamped counters, actual duration/cadence, sample coverage, clock correlation, restoration and attribution scope, provenance and raw artifact paths. GPU and counter deltas SHALL require compatible capture/backend/device/driver, frame, overrides, swapchain, pacing, restoration, cache and counter definitions. It SHALL preserve diagnostics without publishing a numeric improvement when required data is invalid or incomparable.

#### Scenario: Different pacing or counter definitions
- **WHEN** baseline and modified sessions use different effective pacing policies or provider metric definitions
- **THEN** the CLI identifies the mismatch and suppresses a claimed comparable improvement instead of treating their averages as equivalent workloads
