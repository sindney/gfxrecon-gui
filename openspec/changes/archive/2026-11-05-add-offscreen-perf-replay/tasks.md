## 1. Implement mode control and evidence

- [x] 1.1 Add validated CLI/shared swapchain mode selection and local/Android argument forwarding, preserving virtual default behavior.
- [x] 1.2 Emit actual backend swapchain implementation evidence and verify requested/effective mode in GPU reports and explicit non-default CPU measurements.
- [x] 1.3 Include requested mode in pipeline/artifact identities and verified mode in provenance and comparison compatibility checks.

## 2. Verify and document

- [x] 2.1 Add meaningful tests for invalid/missing/mismatched evidence, mode forwarding and cache/comparison segregation; document CLI usage and timing scope. Runtime checks are part of the following build/validation task.
- [x] 2.2 Build the application, desktop backend and Android APK, then validate explicit offscreen mode and matching provenance on representative replay targets. Full core/CLI suite and actual Adreno three-frame offscreen run passed; mode is backend-confirmed and real acquisition is false.
- [x] 2.3 Review the final diff and OpenSpec consistency without committing, pushing or archiving. See `validation.md`.
