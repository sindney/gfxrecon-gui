// Small original launcher for the device's existing OEM Perfetto GPU producer.
// It writes no PID file and never signals another process. The host owns its PID.
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    // Independent finite lifetime if the host disconnects before cleanup.
    // SIGALRM keeps its default terminating action, including if OEM start hangs.
    alarm(900);
    sigset_t stopping; sigemptyset(&stopping); sigaddset(&stopping, SIGTERM); sigaddset(&stopping, SIGINT);
    // OEM threads inherit the mask, so only this main thread consumes our stop
    // request. sigwait avoids the signal-between-check-and-pause race.
    if (sigprocmask(SIG_BLOCK, &stopping, nullptr) != 0) return 4;
    void* library = dlopen("libgpudataproducer.so", RTLD_NOW | RTLD_GLOBAL);
    if (!library) { fprintf(stderr, "OEM GPU producer unavailable: %s\n", dlerror()); return 2; }
    using Start = void (*)(); auto start = reinterpret_cast<Start>(dlsym(library, "start"));
    if (!start) { fprintf(stderr, "OEM GPU producer has no start export\n"); return 3; }
    start();
    int stopped = 0; if (sigwait(&stopping, &stopped) != 0) return 5;
    // No dlclose while the OEM's background threads can still be executing.
    return 0;
}
