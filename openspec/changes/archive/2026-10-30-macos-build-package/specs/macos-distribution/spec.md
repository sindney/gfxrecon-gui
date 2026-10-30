## ADDED Requirements

### Requirement: Native macOS front ends and services
The application SHALL build a native macOS GUI app and JSON CLI using the shared
core. Host tool execution SHALL preserve explicit arguments, environment and
working directory without a shell, support stream transfer and bounded output,
and honor cancellation and timeout. Text/file SHA-256 SHALL retain shared cache
and artifact identity semantics.

#### Scenario: Execute a host tool
- **WHEN** a tool receives Unicode paths, quoted arguments and a working directory
- **THEN** it receives those values intact and its output and exit status are retained

### Requirement: Immutable app bundle and shared user state
macOS samples, tools, licenses and the supplied logo as ICNS SHALL reside in
Contents/Resources. GUI/CLI settings, sessions and caches SHALL share the user's
Application Support directory by default and accept an explicit --settings-dir.
Launching either front end SHALL NOT modify signed bundle contents.

#### Scenario: Open an extracted app
- **WHEN** the GUI opens a bundled sample and the companion CLI inspects it
- **THEN** resources resolve within the relocated bundle, user state is external,
  and the bundle signature and file inventory remain valid after shutdown

### Requirement: Verified macOS release artifacts
Packaging SHALL produce the native app, CLI launcher, host/shader tools, samples,
licenses, ZIP and SHA-256 checksum. It SHALL record file hashes, tool versions,
source state and signing status, and verify the extracted ZIP outside the source
tree with a clean PATH. Android tools/APKs SHALL be included only when explicitly
configured, and package verification SHALL NOT access devices.

#### Scenario: Validate the extracted distribution
- **WHEN** the package is extracted outside the repository
- **THEN** hashes, signatures, permissions, sample decoding, shader round trips,
  GUI startup and Launch Services startup pass using bundled tools

### Requirement: Explicit runtime and tool configuration
Local macOS replay SHALL document its external Vulkan loader/driver prerequisite
and use allocations compatible with the local GPU. The GUI SHALL expose SDK
guidance without blocking offline inspection. Tool discovery SHALL retain bundled
locations, PATH lookup and manual overrides; Android source builds SHALL accept
explicit SDK/JDK roots without broad automatic installation probing.

#### Scenario: Inspect without a local Vulkan runtime
- **WHEN** a user has configured the converter and shader tools but no local runtime
- **THEN** offline inspection remains available and local replay prerequisites
  are explained separately from Android device/build requirements
