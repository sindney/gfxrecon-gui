#import "platform/display_mac.h"

#if PLATFORM_MACOS
#import <Cocoa/Cocoa.h>

namespace macdisplay {

void EnableHighDpi(void* nsWindow) {
    NSWindow* window = (NSWindow*)nsWindow;
    NSOpenGLView* view = (NSOpenGLView*)[window contentView];
    [view setWantsBestResolutionOpenGLSurface:YES];
    [[view openGLContext] update];
}

float BackingScaleFactor(void* nsWindow) {
    NSWindow* window = (NSWindow*)nsWindow;
    return window ? (float)[window backingScaleFactor] : 1.0f;
}

} // namespace macdisplay
#endif
