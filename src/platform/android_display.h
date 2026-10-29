#pragma once
#include "platform/adb.h"
#include <memory>

namespace gfx {
struct AndroidPanelState { std::string physicalId; bool off=false; };
AndroidPanelState ReadAndroidPanelState(const AdbConnection& adb,bool cleanup=false);
void RestoreAndroidReplayDisplay(const AdbConnection& adb,const std::filesystem::path& journal);
class AndroidReplayDisplay {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    AndroidReplayDisplay(const AdbConnection& adb,const std::filesystem::path& journal);
    ~AndroidReplayDisplay();
    void TurnOff();
    void Observe();
    void Restore();
    unsigned OffObservations()const;
};
}
