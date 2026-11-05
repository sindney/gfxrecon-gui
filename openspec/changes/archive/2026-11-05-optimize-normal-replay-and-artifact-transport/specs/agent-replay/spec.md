## MODIFIED Requirements

### Requirement: Replay provenance

Every replay result SHALL include the replay manifest path with capture hash, backend hash, target and device identity, overrides with hashes, request options and artifact hashes, as produced by the shared replay core. GPU measurement provenance SHALL additionally identify the effective replay GPU, Vulkan API and driver properties, device OS/build, effective capture/optimization settings, instrumentation and completion mode, and controlled cache policy and observed pipeline-cache reuse/identity. Uncontrolled driver and hardware cache state SHALL be identified as unknown in GPU reports. For GPU measurement/comparison requests, every requested override SHALL have structured successful application evidence matching its effective bytes; replacement-file discovery alone SHALL NOT establish successful application. Whether a replacement executes within measured scope SHALL be distinguished from successful replacement creation. Ordinary and CPU-only replay SHALL retain their existing backend compatibility and replacement checks.

#### Scenario: Reproduce an agent's result
- **WHEN** a user inspects a replay result directory produced by the CLI
- **THEN** its manifest identifies the exact capture, backend, device and override files used, and the GUI can open the same capture and replay the same selection

#### Scenario: Actual replay driver differs from system probe
- **WHEN** Android selects a different driver for the replay process than a standalone system Vulkan probe
- **THEN** GPU measurement provenance identifies the actual replay driver and does not substitute the standalone probe's driver properties

#### Scenario: Requested replacement not successfully applied
- **WHEN** any shader replacement requested for a GPU comparison lacks successful backend application evidence matching its hash
- **THEN** the performance comparison fails and identifies the unconfirmed module rather than reporting an optimization result

#### Scenario: No explicit pipeline cache loaded
- **WHEN** the replay loads no target pipeline-cache file
- **THEN** provenance records that fact and does not imply that driver or hardware caches were cold

#### Scenario: Historical Android result cache hit
- **WHEN** ordinary replay restores a validated result without contacting the explicitly selected phone
- **THEN** recorded device identity is identified as historical through device_identity_source=cached_result and current_device_verified=false, while fresh/validation/measurement requests retain their live replay requirements
