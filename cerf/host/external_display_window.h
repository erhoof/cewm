#pragma once

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "presenter_canvas.h"
#include "window_fullscreen.h"

class CerfEmulator;
class FrameSource;

/* Standalone titled top-level window presenting one FrameSource (e.g. a PCMCIA
   VGA card) via a shared PresenterCanvas child; own UI thread, View-only menu,
   multi-instance, NOT a Service. on_eject (optional) fires when the user closes
   the window, letting the owner tear it down off this thread. */
class ExternalDisplayWindow {
public:
    ExternalDisplayWindow(CerfEmulator& emu, FrameSource& source,
                          std::wstring title,
                          std::function<void()> on_eject = {});
    ~ExternalDisplayWindow();

    ExternalDisplayWindow(const ExternalDisplayWindow&)            = delete;
    ExternalDisplayWindow& operator=(const ExternalDisplayWindow&) = delete;

    /* Spawn the UI thread and show the window sized to surf_w x surf_h. Blocks
       until the window exists. Idempotent: a second call is a no-op. */
    void Open(uint32_t surf_w, uint32_t surf_h);

    /* Tear down the UI thread + window. Idempotent; also run by the dtor. */
    void Close();

    /* Any thread. Re-size the presented surface (the producer's native frame
       changed, e.g. a VGA mode-set) and fit the window to it. */
    void SetSurfaceSize(uint32_t w, uint32_t h);

    /* Any thread. Update the title bar. */
    void SetTitle(std::wstring title);

private:
    void UiThreadMain(uint32_t surf_w, uint32_t surf_h);
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    HMENU BuildMenu();
    void  SyncMenuChecks();
    void  FitToSurface(uint32_t sw, uint32_t sh);

    CerfEmulator& emu_;
    FrameSource&  source_;
    std::function<void()> on_eject_;

    std::wstring  title_;
    std::mutex    title_mutex_;

    std::thread             ui_thread_;
    std::atomic<bool>       ui_ready_{false};
    std::mutex              ui_ready_mutex_;
    std::condition_variable ui_ready_cv_;

    HWND hwnd_ = nullptr;

    /* Child drawable, fed by the producer. No host hooks: a VGA window has no
       tabs and no stylus. */
    PresenterCanvas canvas_;

    WindowFullscreen fullscreen_;
};
