#pragma once

#include "analysis/capture.h"

namespace gfx {
// Inclusive capture-relative frame range. The returned plan is bound to the
// unoptimized imported capture and contains no replayed or inferred GPU times.
Json BuildGpuMeasurementPlan(const Capture& capture, uint32_t first, uint32_t last,
                             const std::atomic_bool* cancel = nullptr);

// Reject incomplete, stale, mismatched, unsupported or arithmetically invalid
// backend evidence before a GPU sample can enter a CLI comparison.
void VerifyGpuMeasurementReport(const Json& request, const Json& report);

// Counter-clock failures downgrade only this optional alignment evidence. Call
// after strict GPU verification and retain the returned unavailable reason.
Json ValidateFrameCounterWindow(const Json& request, const Json& report);

// Backend log evidence comes from the selected swapchain implementation. Older
// default CPU replays may report effective=null; explicit non-default modes fail
// without confirmation rather than claiming an unobserved implementation.
Json VerifyReplaySwapchain(const std::string& requested, const std::string& log,
                          bool requireConfirmation);
}
