> Superseded by the user's decision to use fresh outer replay sessions. Work now
> follows `simplify-frame-performance-sessions`. Unchecked restoration tasks below
> were retired, not completed; they must not be implemented or claimed delivered.

## 1. Prove and implement supported lifetime restoration

- [x] 1.1 Inventory actual frame 30/60 creation, binding, destruction, memory and recording dependencies and define the ordinary supported policy.
- [ ] 1.2 Implement host preflight with live loop-created binding checks, explicit unsupported forms and resource capability requirements.
- [ ] 1.3 Implement allocator-aware native baseline preservation, fresh transient creation/destruction and GPU-idle cleanup before baseline restoration, including view/memory/recording metadata.
- [ ] 1.4 Add focused native/planner fixtures for repeated create/bind/destroy, alias/dependency/handle cleanup, failures and unchanged ordinary replay.

## 2. Repair failure evidence

- [ ] 2.1 Claim only fresh owned outputs and cover all initialization/preflight/snapshot/collection/report phases with stage-aware receipts, preserving original errors and old evidence.
- [ ] 2.2 Return only existing current-session failure links and add regressions for preflight, invalid/reused output, override setup, cancellation and device-busy semantics.

## 3. Validate the real workflow

- [ ] 3.1 Build matching desktop replay/CLI/GUI and Android APK/capability artifacts and pass focused regression checks.
- [x] 3.2 Run ordinary matched-mode frame 30/60 replay on the connected device and retain output/reference/provenance evidence.
- [ ] 3.3 Run actual sustained bandwidth sessions on frames 30/60, verify required samples, GPU timestamps, restoration work, resource bounds and repeated output correctness.
- [ ] 3.4 Run alternating baseline/same-byte modified controls with confirmed overrides and report comparison validity without claiming an optimization result.

## 4. Review and deliver

- [ ] 4.1 Document actual supported limits, failure semantics, measured device validation and remaining limitations; validate OpenSpec and source diffs.
- [ ] 4.2 Deploy verified matching artifacts in place to dist with receipts/backups, preserving user data and leaving all work uncommitted/unpushed/unarchived.
