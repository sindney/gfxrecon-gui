## ADDED Requirements

### Requirement: Repetition occurs around fresh replay sessions

Profiling SHALL execute a selected frame once through normal replay per process. Repetition SHALL occur in the outer controller, preserving alternating baseline/modified runs. The profiling path SHALL NOT snapshot/restore Vulkan resources or loop a frame inside the process.

#### Scenario: Multiple samples requested
- **WHEN** three profiling runs with an override are requested
- **THEN** the controller performs three independent baseline/modified replay pairs with individual evidence

### Requirement: GPU timestamp timing remains authoritative

GPU duration SHALL retain device timestamp support, availability, scope, completion and validity checks. Optional counter sampling/correlation failures SHALL NOT discard an otherwise valid GPU duration.

#### Scenario: Counter window is sparse or unavailable
- **WHEN** valid GPU timestamps exist but counter collection or clock correlation is unavailable
- **THEN** GPU timing remains available and counter data retains an explicit unavailable/sparse status and reason

### Requirement: Optional counters describe the selected GPU time window

Counter profiles SHALL be opt-in reference data, with `none` the default. Available counter samples SHALL be filtered by the selected frame's correlated GPU start/end timestamps and plotted relative to that frame. Full raw evidence SHALL remain available separately. Empty windows SHALL have null means; sparse windows SHALL retain observed samples and quality metadata without fabricated zeros.

#### Scenario: Counters include loading and cleanup samples
- **WHEN** a trace contains samples before, during and after the selected GPU interval
- **THEN** only the interval samples contribute to displayed means/lines, while the complete trace remains on disk

### Requirement: Profiling UI and controls remain simple

The Perf panel SHALL show frame/profile/run settings, GPU timing, counter tables and frame-relative plots. It SHALL remove FPS/duration/warm-up/restoration controls and explain counter sampling limits in a tooltip and docs. Separate runs SHALL remain distinct graph series.

#### Scenario: User requests timing only
- **WHEN** the default profile is used
- **THEN** ordinary single-frame GPU timing runs without starting a counter provider

### Requirement: Clean migration and reliable diagnostics

Experimental sustained options SHALL be removed with documented replacement usage. Ordinary inspection/replay and legacy non-counter perf SHALL remain compatible. Claimed performance outputs SHALL retain stage-aware failures, and error links SHALL refer only to actual current-session evidence.

#### Scenario: User invokes an obsolete inner-loop option
- **WHEN** an old sustained/FPS/duration/warm-up option is supplied
- **THEN** command validation rejects it without starting replay and documentation identifies the outer-run replacement

### Requirement: Device validation precedes completion

The refactor SHALL be tested on the connected device and available Deferred_Adreno650 capture, including frames 30/60 under ordinary `rebind`, delivered optional bandwidth samples, valid timestamps and outer baseline/modified controls. Rendering and sample quality limitations SHALL be recorded honestly.

#### Scenario: Real capture is profiled
- **WHEN** the selected frames contain resource creation/destruction
- **THEN** normal replay handles them without restoration-specific rejection and artifacts retain measured GPU timing plus available counter-window evidence
