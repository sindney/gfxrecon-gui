// GfxReconGui protocol 1: per-launched-process, read-only manual-reset event.
static bool UpdateGuiCaptureEvent()
{
#if defined(_WIN32)
    struct GuiEvent
    {
        HANDLE handle = nullptr;
        bool configured = false;
        GuiEvent()
        {
            const auto name = util::platform::GetEnv("GFXRECON_GUI_CONTROL_EVENT");
            if (!name.empty())
            {
                configured = true;
                const std::wstring wide(name.begin(), name.end());
                handle = OpenEventW(SYNCHRONIZE, FALSE, wide.c_str());
                if (handle) { GFXRECON_LOG_INFO("GfxReconGui capture control ready (protocol 1)"); }
                else { GFXRECON_LOG_ERROR("GfxReconGui capture event could not be opened"); }
            }
        }
        ~GuiEvent() { if (handle) CloseHandle(handle); }
    };
    static GuiEvent event;
    if (event.configured)
        externally_set_trimming_state_g = event.handle && WaitForSingleObject(event.handle, 0) == WAIT_OBJECT_0;
    return event.configured;
#else
    return false;
#endif
}
