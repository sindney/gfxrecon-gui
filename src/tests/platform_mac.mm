#include "gui/fonts.h"
#include "platform/dialogs.h"
#include "platform/os_layer.h"

#import <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>
#include <imgui_te_coroutine.h>

#include <algorithm>
#include <array>
#include <memory>

namespace tpl {

namespace {
thread_local CGLContextObj coroutineContext = nullptr;
struct CoroutineStart {
    ImGuiTestCoroutineMainFunc* function;
    void* data;
    CGLContextObj context;
};
void RunCoroutine(void* opaque) {
    std::unique_ptr<CoroutineStart> start(static_cast<CoroutineStart*>(opaque));
    coroutineContext = start->context;
    CGLSetCurrentContext(coroutineContext);
    start->function(start->data);
    CGLSetCurrentContext(nullptr);
}
}

void ConfigureMacTestCoroutines(ImGuiTestEngine* engine) {
    // TestFunc runs on a serialized helper thread. Transfer the GL context with
    // each yield so direct workspace operations can safely release GPU resources.
    static ImGuiTestCoroutineInterface interface = *Coroutine_ImplStdThread_GetInterface();
    interface.CreateFunc = [](ImGuiTestCoroutineMainFunc* function, const char* name, void* data) {
        auto start = std::make_unique<CoroutineStart>(CoroutineStart{function, data, CGLGetCurrentContext()});
        const auto handle = Coroutine_ImplStdThread_GetInterface()->CreateFunc(RunCoroutine, name, start.get());
        start.release();
        return handle;
    };
    interface.RunFunc = [](ImGuiTestCoroutineHandle handle) {
        const auto context = CGLGetCurrentContext();
        CGLSetCurrentContext(nullptr);
        const bool running = Coroutine_ImplStdThread_GetInterface()->RunFunc(handle);
        CGLSetCurrentContext(context);
        return running;
    };
    interface.YieldFunc = [] {
        CGLSetCurrentContext(nullptr);
        Coroutine_ImplStdThread_GetInterface()->YieldFunc();
        CGLSetCurrentContext(coroutineContext);
    };
    ImGuiTestEngine_GetIO(engine).CoroutineFuncs = &interface;
}

struct NativeDialogVars {
    bool tested = false;
};

void RegisterMacTests(ImGuiTestEngine* engine) {
    ImGuiTest* t = IM_REGISTER_TEST(engine, "shell", "macOS native window and Retina rendering");
    t->GuiFunc = [](ImGuiTestContext* ctx) {
        NSWindow* window = (NSWindow*)os::NativeWindowHandle();
        IM_CHECK([window isKindOfClass:[NSWindow class]]);

        int width = 0, height = 0;
        os::FramebufferSize(width, height);
        NSRect backing = [[window contentView] convertRectToBacking:[[window contentView] bounds]];
        IM_CHECK_EQ(width, (int)backing.size.width);
        IM_CHECK_EQ(height, (int)backing.size.height);

        const ImGuiIO& io = ImGui::GetIO();
        IM_CHECK_EQ(width, (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x));
        IM_CHECK_EQ(height, (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        IM_CHECK_EQ(ImGui::GetFontSize(), 16.0f * SystemDpiScale());

        std::array<unsigned char, 64 * 64 * 4> pixels;
        GLint readBuffer = 0;
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glReadBuffer(GL_FRONT);
        glReadPixels(0, height - 64, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glReadBuffer(readBuffer);
        IM_CHECK_EQ(glGetError(), (GLenum)GL_NO_ERROR);
        bool varied = false;
        for (size_t i = 4; i < pixels.size(); i += 4)
            varied |= !std::equal(pixels.begin(), pixels.begin() + 3, pixels.begin() + i);
        IM_CHECK(varied);
    };

    t = IM_REGISTER_TEST(engine, "shell", "macOS all-files dialog cancels cleanly");
    t->SetVarsDataType<NativeDialogVars>();
    t->GuiFunc = [](ImGuiTestContext* ctx) {
        NativeDialogVars& vars = ctx->GetVars<NativeDialogVars>();
        if (vars.tested)
            return;
        vars.tested = true;

        __block bool sawPanel = false;
        __block bool unrestricted = false;
        NSTimer* timer = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer*) {
            NSWindow* window = [NSApp modalWindow];
            if ([window isKindOfClass:[NSOpenPanel class]]) {
                NSOpenPanel* panel = (NSOpenPanel*)window;
                sawPanel = true;
                unrestricted = [[panel allowedContentTypes] count] == 0;
                [panel cancel:nil];
            }
        }];
        [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
        auto selected = OpenFileDialog("All files", "*");
        [timer invalidate];
        IM_CHECK(sawPanel);
        IM_CHECK(unrestricted);
        IM_CHECK(!selected.has_value());
    };
}

} // namespace tpl
