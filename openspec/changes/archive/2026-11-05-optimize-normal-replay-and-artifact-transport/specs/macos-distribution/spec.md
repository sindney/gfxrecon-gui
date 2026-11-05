## ADDED Requirements

### Requirement: Native ASTC and configured artifact helper packaging
macOS builds SHALL discover and package a pinned native ASTC encoder with verified acquisition and license provenance. Extracted-package verification SHALL exercise its encode/decode under a clean PATH. Configured ADB and Android artifact-helper assets SHALL resolve from bundle resources. The helper's protocol, ABI and binary hash SHALL be checked without executing Android code during host-only package acceptance.

#### Scenario: Run outside the development checkout
- **WHEN** the extracted distribution is verified with SDK/PATH development tools excluded
- **THEN** native ASTC decoding is available and configured bundled device assets resolve with matching inventory/capability hashes
