# Verification and remaining findings

- Native macOS ARM64 Release GUI, CLI and pinned host tools compiled and ran.
- Desktop sample checks passed core and 20 GUI cases, including shader changes,
  indexed/post-VS geometry and indirect diagnostic views, with no ImGui errors
  or ID conflicts.
- ZIP checks passed hashes, executable permissions, strict/deep code signing,
  clean-PATH tools, four sample imports, shader round trip, six local frame/full
  draw replays, direct GUI and Launch Services launch. The package stayed unchanged.
- Android 16 arm64 testing reused debuggable replay/sample APKs already installed
  on the phone. Their hashes and provenance are retained in ignored local evidence.
  The APKs were not rebuilt on this Mac; Android SDK/JDK source-build setup remains
  unverified here.
- All 15 real-device GUI checks passed after fixing capture-completion focus loss.
  CLI checks passed two on-demand three-frame captures without restarting between
  triggers, cache reuse, full draw inspection, three buffer files and cube counts
  of 12 submitted triangles/eight unique vertices.
- Screen-off replay pixels matched normal replay. Capture settings/properties,
  five capture journals and owned display changes were restored.
- **Validation is not clean:** the installed replay APK emitted two Vulkan 1.0
  vkGetPhysicalDeviceFeatures2 API-version violations and one missing
  VK_KHR_create_renderpass2 dependency for VK_KHR_depth_stencil_resolve. These are
  recorded backend follow-up findings, not a claimed validation pass.
- Optional ASTC encode/decode round-trip checks were skipped because no native
  astcenc decoder was configured. Packages remain ad-hoc signed, not notarized.

Detailed evidence is local under .sessions/macos and .sessions/android, including
results.json, validation logs, APK hashes, restoration audits and screenshots.
Builds, archives, APKs, captures, machine paths and device identifiers are not
committed. Public setup and package instructions are consolidated in docs/BUILD.md.
