#include "platform/dialogs.h"

#include "core/log.h"
#include "platform/os_layer.h"
#include "platform/platform.h"

#include <imgui.h>
#include <nfd.h>

#include <cstring>

namespace tpl {

namespace {

struct NfdSession {
    NfdSession() { NFD_Init(); }
    ~NfdSession() { NFD_Quit(); }
};

nfdwindowhandle_t ParentHandle() {
    nfdwindowhandle_t handle = {};
    void* native = os::NativeWindowHandle();
#if PLATFORM_WINDOWS
    handle.type = NFD_WINDOW_HANDLE_TYPE_WINDOWS;
    handle.handle = native;
#elif PLATFORM_MACOS
    handle.type = NFD_WINDOW_HANDLE_TYPE_COCOA;
    handle.handle = native;
#else
    (void)native;
#endif
    return handle;
}

} // namespace

std::optional<std::string> OpenFileDialog(const char* filterName,
                                          const char* filterSpec) {
    NfdSession session;

    nfdu8filteritem_t filter = {filterName, filterSpec};
    nfdopendialogu8args_t args = {};
    if (filterSpec && *filterSpec && std::strcmp(filterSpec, "*") != 0) {
        args.filterList = &filter;
        args.filterCount = 1;
    }
    args.parentWindow = ParentHandle();

    nfdu8char_t* outPath = nullptr;
    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

    // The modal dialog can swallow modifier key-up events, leaving e.g. Ctrl
    // stuck in ImGui's key state (marp-gui / ue4_memreporter fix).
    ImGui::GetIO().ClearInputKeys();

    std::optional<std::string> selected;
    if (result == NFD_OKAY) {
        selected = outPath;
        NFD_FreePathU8(outPath);
    } else if (result == NFD_ERROR) {
        LOGE("dialog") << "NFD_OpenDialogU8_With failed: " << NFD_GetError();
    }
    return selected;
}

#ifdef IMGUI_TEMPLATE_TESTS
static SaveDialogOverride saveDialogOverride = nullptr;
void SetSaveDialogOverrideForTests(SaveDialogOverride callback) { saveDialogOverride = callback; }
#endif
std::optional<std::string> SaveFileDialog(const char* name, const char* extensions, const char* defaultName, const char* defaultDirectory) {
#ifdef IMGUI_TEMPLATE_TESTS
    if(saveDialogOverride){auto result=saveDialogOverride(name,extensions,defaultName,defaultDirectory);ImGui::GetIO().ClearInputKeys();return result;}
#endif
    NfdSession session;
    nfdu8filteritem_t filter{name, extensions};
    nfdsavedialogu8args_t args{}; args.filterList = &filter; args.filterCount = 1;
    args.defaultName = defaultName; args.defaultPath = defaultDirectory; args.parentWindow = ParentHandle();
    nfdu8char_t* path = nullptr;
    const auto result = NFD_SaveDialogU8_With(&path, &args);
    ImGui::GetIO().ClearInputKeys();
    if (result == NFD_OKAY) { std::string selected(path); NFD_FreePathU8(path); return selected; }
    if (result == NFD_ERROR) LOGE("dialog") << "Cannot choose export path: " << NFD_GetError();
    return std::nullopt;
}
} // namespace tpl
