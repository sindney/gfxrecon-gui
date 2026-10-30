#pragma once
#include "capture/capture.h"
#include <functional>
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

// ---- Stateless Android on-demand capture (shared by LiveCaptureSession and `gfxrecon-cli capture ...`) ----
// All state between calls lives in the session directory: device-journal.json (schema 2, the recovery journal shared with
// the GUI recovery and `capture restore`) and session.json (package, staging path, layer-log offset, capture counter).
struct AndroidArmOptions {
    std::filesystem::path directory;     // session directory; must not already hold a device journal
    std::string package, arguments;
    uint32_t frames = 3;                 // frames per capture, fixed at launch by the layer (0: manual start/stop, GUI only)
    bool launch = true;                  // false: journal and apply settings only; the next launch of the app is captured
    std::chrono::seconds timeout{120};   // layer initialisation wait
};
struct AndroidSessionState {
    std::filesystem::path directory;
    std::string serial, package, relative, remote;
    uint32_t frames = 0, captures = 0;
    uint64_t logOffset = 0;              // bytes of the layer log already consumed (complete lines only)
    int64_t lastTriggerMs = 0;           // system clock; the layer needs a frame boundary between triggers
    bool launched = false, ended = false;
    bool prepared = false;               // not persisted: the recovery journal exists and may need restoring
    std::filesystem::path Journal() const { return directory / "device-journal.json"; }
};
struct AndroidCaptureFile {
    std::filesystem::path path;
    uint64_t bytes = 0;
    std::string sha256;
    uint32_t frames = 0;                 // frames per trigger the session was armed with
};
struct AndroidLogLine { std::string text; uint64_t begin = 0, end = 0; };
// Thrown by restore when device settings were changed outside the session; `conflicts` lists name/expected/previous/current.
class AndroidRestoreConflict : public std::runtime_error {
public:
    Json conflicts;
    std::filesystem::path journal;
    AndroidRestoreConflict(const std::string& message, Json list, std::filesystem::path file)
        : std::runtime_error(message), conflicts(std::move(list)), journal(std::move(file)) {}
};
AndroidSessionState LoadAndroidSession(const std::filesystem::path& directory); // throws when session.json is missing
void SaveAndroidSession(const AndroidSessionState& state);
Json AndroidSessionJson(const AndroidSessionState& state);
// Throws AndroidUnlockRequired when the keyguard is showing. Never wakes or dismisses anything.
void RequireAndroidUnlocked(const AdbConnection& adb);
// Journals prior settings, applies the capture settings and (unless options.launch is false) launches the app tracking-armed.
// `state` is filled progressively so a failing caller can clean up (state.prepared / state.launched).
void PrepareAndroidCapture(const AdbConnection& adb, const AndroidArmOptions& options, AndroidSessionState& state);
// Waits for the layer to create its log (the armed signal). Returns false when `stop` asked to give up; throws when the app
// dies, the layer never starts, the session is cancelled, or capture began at launch (the app is then closed).
bool WaitAndroidArmed(const AdbConnection& adb, AndroidSessionState& state, std::chrono::seconds timeout, const std::function<bool()>& stop = {});
// Prepare + wait. On failure the app is closed and the settings restored (the message notes a pending recovery otherwise).
AndroidSessionState ArmAndroidCapture(const AdbConnection& adb, const AndroidArmOptions& options,
                                      const std::function<void(const std::string&)>& progress = {});
// Sets/resets debug.gfxrecon.capture_android_trigger through the journal. Enabling checks the lock screen first and
// waits for a frame boundary since the previous change.
void SetAndroidTrigger(const AdbConnection& adb, AndroidSessionState& state, bool enabled, bool cleanup = false);
// Complete new layer-log lines since the persisted offset (does not advance it; see AdvanceAndroidLog).
std::vector<AndroidLogLine> ReadAndroidLogLines(const AdbConnection& adb, const AndroidSessionState& state);
void AdvanceAndroidLog(AndroidSessionState& state, uint64_t offset, bool save = true);
// Pulls a finished capture into <session>/captures/capture-N.gfxr with a capture-N.json sidecar and verifies it.
AndroidCaptureFile CollectAndroidCapture(const AdbConnection& adb, AndroidSessionState& state, const std::string& remoteFile);
// Records one capture of the armed frame count and retrieves it (plus any finished capture an interrupted trigger left
// behind). A locked phone throws AndroidUnlockRequired before the trigger is touched; the session stays armed.
// `frames` 0 uses the armed count; any other value must equal it (the layer fixes it at launch).
std::vector<AndroidCaptureFile> TriggerAndroidCapture(const AdbConnection& adb, const std::filesystem::path& directory, uint32_t frames,
                                                      std::chrono::seconds timeout, const std::function<void(const std::string&)>& progress = {});
Json AndroidCaptureStatus(const AdbConnection& adb, const std::filesystem::path& directory);
// Optionally closes the game, then restores every journaled setting. Throws AndroidRestoreConflict on conflicts.
Json EndAndroidCapture(const AdbConnection& adb, const std::filesystem::path& directory, bool closeGame);
// RestoreAndroidCapture with structured conflict reporting (works for any capture journal).
void RestoreAndroidJournal(const AdbConnection& adb, const std::filesystem::path& journal);
}
