## Why

Image previews show the selected zero-based mip index without the texture's mip count, and normal draw inspection fetches only level zero. Draw diagnostics currently affect only color outputs, making it difficult to relate geometry to depth and stencil previews.

## What Changes

- Show actual mip counts and a mip dropdown in image previews; fetch missing input levels on demand without reading every bound texture's mip chain.
- Keep one document/sidebar entry per binding when changing mip levels, preserving compressed-texture decoding and stale-selection protection.
- Add display-only geometry diagnostics over original depth/stencil images while preserving scalar values, range controls and pixel readout.
- Clearly distinguish geometry coverage/depth comparison from actual depth/stencil writes; retain explicit unavailable reasons for unsupported or mismatched attachments.

## Capabilities

### New Capabilities
- `image-subresource-preview`: Accurate image-level metadata and on-demand mip selection.
- `scalar-diagnostic-overlays`: Independent screen-space diagnostic layers over depth and stencil previews.

### Modified Capabilities

None.

## Impact

Image/resource GUI, process-local draw cache, replay diagnostic provenance and optional coverage renders, native replay adapter, tests, documentation and portable artifacts. No new dependencies.
