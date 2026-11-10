## Why

GPU counters already work for Adreno, but Mali support has not been validated and errors still describe the helper as Adreno-only. The connected Mali device exposes an OEM Perfetto producer with 363 counters, allowing support to be verified against real hardware now.

## What Changes

- Support Mali through the shared OEM Perfetto producer launch, discovery, capture, and Perf panel flow, following AGI's library/start entry point rather than introducing a second driver backend.
- Retain the registered producer identity and useful helper launch diagnostics in saved evidence, with vendor-neutral errors.
- Handle Mali's explicit dimensionless `NONE` unit factors in display labels while preserving raw unit definitions and samples.
- Convert Mali MONOTONIC/MONOTONIC_RAW samples to BOOTTIME using trace clock snapshots, retaining original timestamps and explicit alignment limits.
- Add Mali descriptor/trace and display regression coverage and document real-device acceptance results and limitations.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `gpu-counter-collection`: Explicit Mali OEM provider support, producer provenance, launch diagnostics, and dimensionless unit presentation.

## Impact

Shared Android counter discovery/decoding, performance series display, tests, and CLI/GUI/build documentation. No new external dependencies or CLI flags. The existing Android helper remains usable for Adreno and Mali; device support depends on an accessible OEM producer, not just the GPU vendor name.
