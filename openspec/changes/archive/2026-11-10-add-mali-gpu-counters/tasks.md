## 1. Shared Mali capture support

- [x] 1.1 Retain matching registered producer identity in counter selections and make helper errors vendor-neutral.
- [x] 1.2 Save owned helper diagnostics before cleanup and surface launch/registration failures.
- [x] 1.3 Normalize explicit NONE factors only in performance display scaling, preserving raw units and values.
- [x] 1.4 Normalize Mali MONOTONIC/MONOTONIC_RAW timestamps through validated trace snapshots and request periodic snapshot evidence.

## 2. Regression coverage

- [x] 2.1 Add Mali protobuf catalog/trace coverage for high IDs, NONE factors, raw counts, ratios, and generic window aggregation, including producer identity.
- [x] 2.2 Verify Mali display scaling and run counter/performance tests plus relevant GUI checks.
- [x] 2.3 Cover snapshot clock conversion, missing/contradictory bridges, overflow, and unsupported clock domains.

## 3. Device acceptance and documentation

- [x] 3.0 Make the generated Android test sample manifest declare replay-layer package visibility for fresh captures on Android 16.
- [x] 3.1 Build host/helper binaries and verify fresh Mali frame counter capture, report/CSV evidence, and owned cleanup on the connected device.
- [x] 3.2 Verify Mali counters in the Perf panel and document support, prerequisites, AGI references, and actual acceptance limitations.
