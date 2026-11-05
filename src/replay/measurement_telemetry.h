#pragma once

#include "analysis/capture.h"
#include "platform/process.h"
#include <functional>

namespace gfx {

using MeasurementTelemetryTransport =
    std::function<ProcessResult(const ProcessRequest&, const std::atomic_bool*)>;

// Parses only the service's explicit current-HAL block for GPU temperatures.
// Host observation times are added by QueryMeasurementTelemetry.
Json ParseAndroidThermalTelemetry(const std::string& text);

// Optional snapshots for use before/after replay, never inside measured GPU work.
// Missing/denied sources return labelled statuses. Cancellation propagates.
// An empty executable or serial returns unsupported metadata for local replay.
Json QueryMeasurementTelemetry(const std::string& adb, const std::string& serial,
                               const std::atomic_bool* cancel = nullptr,
                               const MeasurementTelemetryTransport& transport = {});

} // namespace gfx
