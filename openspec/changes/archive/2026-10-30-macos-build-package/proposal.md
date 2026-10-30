## Why

The GUI bundle existed on macOS, but shared process/hash services and release
packaging still assumed Windows. Local sample and real Android tests exposed
portability issues and capture-completion focus loss.

## What Changes

- Build and run the GUI/CLI and shared host tools natively on macOS.
- Package the existing logo, host/shader tools, samples and licenses in a signed
  app with a CLI launcher, verified ZIP and checksum.
- Keep resources inside the app and mutable settings/caches in Application Support.
- Retain bundled/PATH tool discovery and explicit manual configuration.
- Keep the capture-completion dialog open until the user opens the result.
- Document Windows/macOS setup and record device testing and remaining findings.

## Capabilities

### New Capabilities
- `macos-distribution`: Native services, bundle layout, packaging and prerequisites.

### Modified Capabilities
- `agent-cli`: Platform-neutral front ends and shared configuration/cache roots.
- `inspection-workspace`: Preserve the modal capture-completion stage.

## Impact

CMake, platform services, app paths/icon, release scripts, GUI tests and public
guides. Windows distribution behavior is retained. Android APK compilation on
this Mac and resolution of the installed APK's validation errors are outside
the completed work; see ARCHIVE-NOTES.md.
