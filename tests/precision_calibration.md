# Opt-in fragment precision counter controls

These original sources reproduce the controlled FP32/explicit-FP16 validation. They contain no private capture or proprietary GPU SDK, are not part of normal startup, and add no public `gfxrecon-cli` mode. The native workload is compiled with the Android NDK; the optional host driver links the shared counter/lease core.

Build the native controls without accessing a device:

```powershell
python scripts/validate_precision_counters.py --ndk-root $env:ANDROID_NDK_ROOT --tools local-tools.json --output .sessions/precision-controls
```

Alternatively use `--sdk-root` to select an installed NDK under the SDK's `ndk` directory. Tool paths come from the supplied tools JSON. The script compiles and validates SPIR-V, disassembles the controls, rejects Float16/RelaxedPrecision in the FP32 shader, requires explicit Float16 in the FP16 shader, builds the original ARM64 Vulkan executable, and writes binary/shader/source SHA-256 provenance.

The separately built optional host diagnostic is named `gfxrecon_precision_counter_validation` (binary `gfxrecon-precision-counter-validation`). Build that target explicitly after configuring the project; it is excluded from the normal application build. Before running it, build/configure the original `gpu_counter_producer` helper and its identity sidecar through the normal Android setup or `scripts/build_counter_producer.py`.

```powershell
cmake --build build-agent-replay --config Release --target gfxrecon_precision_counter_validation
.\build-agent-replay\Release\gfxrecon-precision-counter-validation.exe --serial DEVICE --tools local-tools.json --workload-dir .sessions/precision-controls --output .sessions/precision-control-results
```

Or explicitly opt into running from the preparation script using `--run HOST_DRIVER --serial DEVICE --run-output NEW_DIR`. Building without `--run` makes no device calls.

The host driver runs FP32 A, explicit FP16, FP32 B under a global target lease. Defaults are 512 x 512, 256 runtime arithmetic iterations per fragment, and 30 completed frames paced at 30 FPS. `--frames`, `--iterations` and `--fps` are bounded diagnostic controls. It checks shaderFloat16 support/enables the same feature in both variants, verifies uploaded bytes and native shader hashes, waits for pipeline readiness before counter capture, and records BOOTTIME windows, actual driver properties, cadence/missed deadlines, raw traces/descriptors and samples. Pixel readback is outside the counter window. Required counters must return usable samples; missing data is not zero-filled.

Only owned processes/files are stopped/removed. Existing replay/tracing users cause a busy/refusal result. The diagnostic does not unlock/dismiss keyguard, change clock/layer/driver settings, use root, or install an APK. The native executable has an independent watchdog.

Interpretation requires matching work, driver/counter definitions, cadence and FP32 A/B pixel hashes. A directional drop in the full metric with a rise in half under explicit FP16 is descriptive evidence that those fragment metrics respond to the precision control. There is no arbitrary ratio that certifies hardware scalar-op/FLOP normalization. Percentages retain vendor-defined denominators. Counters remain system-wide sampled data; the result does not establish exact process/shader attribution or native FP16 conversion/speedup for a separate RelaxedPrecision edit.

Full A/B images should match. Half images can differ because precision changes numerical rounding; this is not a full/half output-equivalence test. Host submit/fence intervals in the native diagnostic are not GPU timestamp durations and are not used to claim GPU speedup.
