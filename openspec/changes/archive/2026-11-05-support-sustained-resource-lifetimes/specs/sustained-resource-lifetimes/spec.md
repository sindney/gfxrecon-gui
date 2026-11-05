## ADDED Requirements

### Requirement: Ordinary resource lifetimes are restored per iteration

Sustained replay SHALL support validated ordinary buffer/image/image-view/device-memory creation, destruction and binding inside a selected frame. Baseline physical handles SHALL remain valid for existing command buffers/descriptors. Loop-created resources SHALL start each iteration with the captured creation/initialization semantics and SHALL NOT accumulate across repetitions.

#### Scenario: New buffers and image in frame 30
- **WHEN** the selected frame creates ordinary buffers and an image/view and binds their memory
- **THEN** each repeat recreates or equivalently restores their fresh state, cleans transient ownership and restores baseline contents before the next measured iteration

#### Scenario: Baseline image destroyed in frame 60
- **WHEN** the selected loop destroys a baseline image and its view
- **THEN** their physical handles remain safe for loop restoration and dependent recorded state, and destruction is completed during owned teardown

### Requirement: Unsupported resource forms remain explicit

The host and native backend SHALL validate object identity, device scope, creation/binding forms, dependencies and required state. They SHALL reject unproven forms rather than silently omit restoration or return successful incorrect measurements.

#### Scenario: Attempt to rebind a baseline object
- **WHEN** a selected frame attempts a memory binding whose original baseline state cannot be reconstructed safely
- **THEN** the session fails with a specific restoration reason and valid failure evidence

### Requirement: Actual capture and device validate completion

Completion SHALL require sustained replay on the connected real device using the available Deferred_Adreno650 capture, with delivered required bandwidth samples, valid GPU timestamps, bounded repetition state and output-correctness checks. The evidence SHALL identify frames, device/driver, effective memory/swapchain policy, overrides and comparison status.

#### Scenario: Real bandwidth A/B control
- **WHEN** baseline and same-byte modified runs execute the representative frames
- **THEN** result artifacts retain measured GPU/counter samples and correct override provenance, and unsupported or incomparable runs are not presented as successful performance comparisons
