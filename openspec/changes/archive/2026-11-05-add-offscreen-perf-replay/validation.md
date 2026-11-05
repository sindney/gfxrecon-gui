# Offscreen perf validation

Validated on 2026-11-02 without commit, push or archive.

- Application/CLI/core test Release build succeeded.
- Bundled desktop replay Release build and ARM64 debug replay APK build succeeded; capability files were regenerated for exact final binaries. The matching APK was installed only after checking that no replay process or known CLI context owned the phone.
- Focused GPU/telemetry fixtures passed, including missing/mismatched effective mode, old default-backend unknown evidence, invalid CLI value and strict new GPU report mode validation.
- Full core/CLI suite passed with desktop virtual and offscreen GPU control pairs. Offscreen tests require actual backend evidence and ensure virtual/offscreen cache identities differ; no GPU-backend skip occurred.
- Actual matching Adreno 840 game capture, original SHA-256 `3c77f6fa01433cb46595109da7ccff5230634bc38c171cb72b52f78b4e49b1c4`, completed one fresh offscreen run of frames 1-3. All three timestamp samples are valid, approximately 8.722552, 7.706614 and 7.470260 ms. These observations are not a paired performance-improvement claim.
- Backend report records `swapchain.requested = offscreen`, `effective = offscreen`, `source = backend_swapchain_class` and `real_swapchain_acquisition = false`. GPU provenance retains that exact evidence; the replay manifest's request/cache identity records `swapchain_mode = offscreen` before pipeline-cache identity hashing.
- The offscreen request adds no counters, warm-up or repetition. The existing full-frame timestamp interval may still include dependency waits and CPU submission gaps; offscreen mode removes real swapchain acquisition/display presentation from that path without claiming all pacing effects disappeared.

Evidence outside Git: `.sessions/offscreen-perf-validation/android-offscreen/result.json`, per-run GPU report/manifest/logs, `core-tests.log`, and application/backend/APK build logs in the same validation root. Known CLI device byte locks were held during installation/replay, and released afterwards; no other session was stopped.
