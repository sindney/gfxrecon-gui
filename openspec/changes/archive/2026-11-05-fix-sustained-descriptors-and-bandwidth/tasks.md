> Historical experiment: in-process sustained replay/restoration is superseded by
> `simplify-frame-performance-sessions`. Do not archive these inner-loop
> requirements as the current design. Counter profiles and durable failure
> evidence continue in the simpler fresh-session implementation.

## 1. Descriptor lifecycle restoration

- [x] 1.1 Replace the blanket descriptor allocation/free/reset rejection with validated supported-form planning and actionable unsupported reasons.
- [x] 1.2 Implement native baseline/transient descriptor lifetimes, pool ownership, safe mixed allocation/free/reset behavior and cleanup without modifying the pinned submodule.
- [x] 1.3 Preserve/reconstruct supported descriptor writes across iterations, erase stale generations and cover arrays/baseline references with meaningful tests.

## 2. Bandwidth counter profile

- [x] 2.1 Add shared generic profile discovery with compatible precision wrappers and runtime ID/unit/period/capacity validation.
- [x] 2.2 Require read/write bandwidth independently of ALU metrics and disclose optional texture/vertex/shader read availability; test missing/ambiguous/invalid providers.
- [x] 2.3 Wire profile selection through session policy/provenance/comparisons, CLI and GUI with correct capability invalidation.

## 3. Validate and deliver

- [x] 3.1 Run focused counter/planning/lifecycle tests and local native descriptor controls, checking repeated output/state correctness and unchanged ordinary replay.
- [x] 3.2 Build updated desktop CLI/GUI/replay and Android APK, regenerate matching capability hashes and validate without accessing the phone.
- [x] 3.3 Document bandwidth usage/scope and supported descriptor limits; validate OpenSpec and final diffs.
- [x] 3.4 Deploy verified artifacts and notices in place to dist with backups, preserving user data and leaving changes uncommitted/unpushed for review and external device testing.
