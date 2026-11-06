# android-artifact-transport Specification

## Purpose
Transfer Android replay readbacks efficiently with a separately deployed LZ4 helper, checksum-first framing, complete validation and safe publication.

## Requirements
### Requirement: Optional verified native transfer helper
Android replay SHALL support a separately deployed ARM64 artifact helper, identified by SHA-256 and protocol/ABI capability metadata. Deployment SHALL verify uploaded bytes and executable compatibility and reuse a compatible deployed helper. The existing compatible replay APK SHALL not require replacement for this transport feature.

#### Scenario: Missing or unsupported helper
- **WHEN** no compatible executable helper can be used
- **THEN** ordinary artifact retrieval remains available through the raw transport

### Requirement: Checksum-first compressed bundles
Compressed artifact transfer SHALL begin with protocol, codec, uncompressed byte count, payload byte count and XXH64 checksum of the complete uncompressed bundle, before the LZ4 payload. LZ4 block/frame checksums SHALL also be enabled. Artifact names, lengths, content and final stream terminator SHALL be covered by the content checksum.

#### Scenario: Complete compressed download
- **WHEN** a compressed bundle arrives intact
- **THEN** exact lengths, frame completion, content checksum and all artifact records are verified before any readback file is published

### Requirement: Reject incomplete or corrupt readbacks
The receiver SHALL reject malformed headers, unsupported codecs, bad hashes, invalid lengths/names, duplicate artifacts, damaged or incomplete frames, trailing data and invalid artifact records. Rejected compressed data SHALL not be silently accepted by retrying it as raw data. Cancellation SHALL discard private staging data and preserve prior completed artifacts.

#### Scenario: Incorrect leading hash
- **WHEN** the sender supplies the wrong content checksum for an otherwise complete bundle
- **THEN** replay fails with an integrity error and publishes no readbacks from that bundle

#### Scenario: Missing payload bytes or extra frame
- **WHEN** a payload is truncated or has trailing bytes or another frame
- **THEN** completion/length validation rejects the transfer and removes private staging data

### Requirement: Transfer evidence and normal-operation acceptance
Actual replay manifests SHALL record artifact codec, integrity mode, raw bytes and wire bytes. Performance acceptance SHALL measure complete normal GUI draw-selection round trips against raw transfer with matching capture/settings, cache policy and readback hashes; a transfer-only microbenchmark SHALL not establish an overall replay gain.

#### Scenario: Large draw A/B measurement
- **WHEN** raw and LZ4 selections are compared with warm target pipelines and fresh process-local draw inspections
- **THEN** the recorded results include complete selection latency and verify identical image/buffer content
