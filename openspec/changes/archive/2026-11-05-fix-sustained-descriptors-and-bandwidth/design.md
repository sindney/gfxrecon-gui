## Context

The sustained host planner rejects all allocation/free calls and descriptor pool resets. The native restoration overlay snapshots existing descriptor slots only, whereas the underlying frame-loop consumer already tracks some dangling descriptor lifetimes. These mechanisms need explicit coordination before such frames can repeat safely. The OEM Perfetto selector currently requires full/half fragment instruction metrics and excludes advertised bandwidth rates.

## Goals / Non-Goals

Goals are correct supported descriptor lifecycle replay and an independent bandwidth profile through the shared CLI/GUI session. Precision remains the default. Connected-phone testing is reserved for the user's other agent. This change does not support arbitrary Vulkan resource creation, descriptor buffers, acceleration structures, or multiplexed counter passes.

## Decisions

- Handle supported descriptor allocation/free/pool-reset events explicitly. Track set identity, pool ownership and initialized values, and separate pre-loop baseline slots from loop-created lifetimes. Do not restore through freed or invalidated handles. Preserve conservative rejection where a baseline cannot be reconstructed. Validate logical allocation batches and recycled handles instead of removing the existing blanket guard without native support.
- Keep generated backend overlays outside the pinned submodule. Rebuild both host replay and Android APK and bind capability sidecars to their new hashes. Existing single-frame replay behavior stays unchanged.
- Use shared `counterProfile` selection (`precision` default, `bandwidth` opt-in). Runtime descriptors supply IDs and units; bandwidth requires total read/write BYTE/SECOND metrics and includes supported optional texture/vertex/shader-processor rates. Missing optional metrics are reported explicitly. Unknown/ambiguous metrics, invalid periods or insufficient simultaneous capacity fail clearly.
- Record the profile and selected definitions in provenance/comparison policy. Reuse shared tabular/plot/CSV reporting for arbitrary selected metrics; rates remain whole-device sampled whole-loop values, including restoration/pacing. No per-frame byte totals are inferred.
- Exercise pure planning/counter fixtures plus local native descriptor replay controls. Deploy in place with backups and receipts, preserving settings/captures/caches; leave phone validation to the user.

## Risks / Trade-offs

- Recycled descriptor handles and pool resets can invalidate saved values: track allocation generations and erase stale references; test reset/free/reallocate paths and compare repeated GPU outputs.
- Some extended descriptor or mixed lifetime patterns remain unsupported: reject specifically rather than produce misleading successful measurements.
- Providers can advertise counters they fail to deliver: retain packet, clock, coverage and finite-value validation. Discovery alone does not claim successful phone measurements.
- Bandwidth includes restoration traffic: preserve explicit whole-loop scope and pacing/comparison checks.
- Native backend changes require new binaries: stage matching APK/tool sidecars and retain previous artifacts for rollback.

## Migration Plan

No preference/schema migration. Existing precision commands keep their meaning. Users opt into `--counter-profile bandwidth`; GUI users choose the sustained bandwidth mode. Update dist locally after builds/checks, without installing on the connected phone or committing/pushing.
