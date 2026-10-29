#pragma once
#include "capture/capture.h"
#include <memory>

namespace gfx {
enum class LivePhase { Starting, Armed, Triggering, Recording, Stopping, Retrieving, Ended, Failed };
struct LiveCaptureOptions {
    CaptureRequest capture;
    ProcessRequest process;
    std::string layerDirectory, infoTool;
    bool android = false;
    uint32_t frames = 3; // zero: record until Stop
};
struct LiveCaptureState {
    LivePhase phase = LivePhase::Starting;
    std::string detail;
    bool gameRunning = false;
    std::vector<std::filesystem::path> captures;
};
class LiveCaptureSession {
public:
    LiveCaptureSession(LiveCaptureOptions options, const AdbConnection& device = {"",""});
    ~LiveCaptureSession();
    LiveCaptureState State() const;
    void Capture();
    void StopCapture();
    void End(bool closeGame = false);
    bool Active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
