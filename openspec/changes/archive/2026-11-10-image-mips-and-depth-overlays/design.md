## Context

Backend metadata describes each dumped subresource with `mipLevel`; normal GUI requests leave `DumpAllImageSubresources` false. Image creation records carry the real `mipLevels`. Existing dump selectors support a specific descriptor and subresource range. Diagnostic pipelines write color while disabling depth/stencil writes, so their scalar readbacks cannot serve as the original depth/stencil preview.

## Goals / Non-Goals

**Goals:** Accurate mip information, bounded on-demand mip readback, stable image documents, and trustworthy visual annotations over scalar attachments.

**Non-Goals:** Inferring shader-selected LOD per pixel, simulating fragment discard/stencil writes, or synthesizing a color attachment for depth-only passes.

## Decisions

1. Enrich image metadata from captured image creation information; do not treat a missing mip count as zero. Use the existing descriptor/subresource selector for missing input levels. Preserve the current level until the requested result succeeds and reject stale generations.
2. Identify image documents by binding/image/aspect/layer, independent of mip and temporary replay path. Group sidebar entries the same way. Cache fetched levels with the current draw snapshot.
3. Preserve original depth/stencil artifacts in diagnostic views. Render a separately colored variant when needed to obtain a reliable coverage mask, avoiding comparison against arbitrary application colors. Record/verify the variant in backend provenance. Overlay pixels live in a separate RGBA viewer texture after scalar range mapping and share the image's orientation and zoom.
4. Keep scalar source bytes and depth sidecars unchanged. Display geometry/depth-test diagnostics with a tooltip explaining that coverage does not imply scalar writes. Reject mismatched extents or unavailable color masks explicitly. Keep the selected scalar attachment when switching diagnostic modes.

## Risks / Trade-offs

- Extra diagnostic readbacks cost time: create only requested scalar overlays and retain them in the bounded process-local draw cache.
- Compressed mip previews require decoding: use the existing ASTC path and retain the previous preview on failures.
- Old replay backends do not acknowledge mask variants: require matching provenance and show unavailability instead of using an invalid mask.
- Layer/mip alignment must be proven from actual output subresources; do not resize a mask onto an unrelated scalar attachment.
