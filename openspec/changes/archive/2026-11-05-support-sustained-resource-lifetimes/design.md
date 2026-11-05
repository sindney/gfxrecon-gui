## Context

Frames 30 and 60 of the available Deferred_Adreno650 capture require ordinary resource lifetimes in addition to descriptors. Frame 30 creates buffers, an image/view and binds pre-existing memory; frame 60 destroys an existing image/view. The current host guard rejects these before sampling. Session initialization also runs before the failure-artifact catch. Prior fixture-only validation did not establish actual-capture bandwidth support.

## Goals / Non-Goals

Support validated ordinary buffer/image/view/device-memory creation, destruction and binding in a repeated frame, retain accurate failure evidence and complete real-device validation. Preserve existing precision/bandwidth meaning and alternating runs. External, sparse, protected, disjoint, address-based or unproven extended resources remain explicit limitations. A new submission-only replay architecture is not required if full-frame replay can restore the necessary state correctly.

## Decisions

- Preserve baseline physical resource handles and allocator state. Defer their captured destruction/free until teardown so pre-recorded command buffers and descriptors retain valid references. Baseline contents/layouts continue to use the validated shadow restoration path.
- Create transient resources and perform ordinary bindings on every iteration. Execute explicit transient destroys normally and clean remaining transient views, buffers/images and memory through allocator-aware backend paths at GPU idle before baseline restoration. This avoids carrying an earlier iteration's image layout, bytes or native allocation into a logically fresh resource.
- Prove supported creation/binding forms in host preflight and verify actual identity/device/state in native code. Bind only loop-created live objects; Vulkan baseline memory bindings cannot be undone safely. Validate dependent views, destruction order, host mappings, aliasing and command-buffer recording freshness rather than silently skipping lifetime operations.
- Identify resource-lifetime support with a new binary-bound capability marker/protocol and record requested/effective policy and per-iteration work in provenance. Comparison rules must distinguish actual work without treating accumulated repetition totals as different workloads.
- Claim only a fresh owned performance output. Catch stage-aware failures from preflight, backend setup, override snapshots, collection and reporting; write failure.json best-effort while preserving the original exception. CLI links only an actual current-session receipt. Existing evidence is never overwritten to report a rejected reuse.
- Require delivered GPU timestamps and actual bandwidth samples on the connected phone for the sample capture. Check frames 30/60 and a same-byte override A/B control, then compare ordinary and repeated image output under matched mode/state. A fixture-only pass is insufficient.

## Risks / Trade-offs

- New resources can alias baseline memory: clean transient bindings/objects before restoring baseline contents and test actual capture output.
- Removing a handle while a recorded command references it invalidates that recording: preserve baseline handles and prove transient-dependent recordings are refreshed; reject unsupported retained recordings.
- Allocator remap/rebind metadata and mapped host pointers can outlive objects: use ordinary allocator-aware destruction/free paths and audit object-table/address/mapping cleanup.
- Optional counters can be advertised but not delivered: retain required read/write, clock, packet, coverage and finite-value checks; do not label discovery as measurement.
- Secure device lock or provider limitations can block real-device tests: preserve diagnostics and discuss concrete device requirements without claiming completion.

## Migration Plan

Build host tools/APK and regenerate matching capability hashes. Validate using isolated fresh output directories, existing device lease and the user's capture. Deploy updated local dist artifacts with backups/receipts after successful checks, preserving settings/caches/captures. Leave source and OpenSpec uncommitted/unarchived for review.
