#include "../core/cerf_emulator.h"
#include "../core/cerf_paths.h"
#include "../core/device_config.h"
#include "../core/run_timeout.h"
#include "../core/string_utils.h"
#include "host_canvas.h"
#include "host_screenshot.h"
#include "host_window.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

class LiveScreenshotReporter : public Service {
public:
    using Service::Service;

    ~LiveScreenshotReporter() override { Stop(); }
    void OnShutdown() override { Stop(); }

    void OnReady() override {
        const std::string name = emu_.Get<DeviceConfig>().device_name;
        if (name.empty()) return;
        const std::string dir = GetDeviceDir(name);
        path_ = (std::filesystem::path(Utf8ToWide(dir.c_str())) /
                 L"live_state.png").wstring();
        capture_done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!capture_done_) return;
        thread_ = std::thread([this] { Loop(); });
    }

private:
    static constexpr auto kInterval = std::chrono::seconds(10);

    void Stop() {
        stop_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> g(cv_mtx_);
            cv_.notify_all();
        }
        if (capture_done_) SetEvent(capture_done_);  /* unblock a pending wait */
        if (thread_.joinable()) thread_.join();
        if (capture_done_) { CloseHandle(capture_done_); capture_done_ = nullptr; }
        if (!path_.empty() && !RunTimeout::IsActive()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(path_), ec);
        }
    }

    void Loop() {
        std::unique_lock<std::mutex> lk(cv_mtx_);
        while (!stop_.load(std::memory_order_acquire)) {
            lk.unlock();
            Tick();
            lk.lock();
            if (stop_.load(std::memory_order_acquire)) break;
            cv_.wait_for(lk, kInterval);
        }
    }

    void Tick() {
        captured_ = false;
        ResetEvent(capture_done_);
        emu_.Get<HostWindow>().RunOnUiThread([this] {
            captured_ = emu_.Get<HostCanvas>().CaptureGuestSurface(buf_, w_, h_);
            SetEvent(capture_done_);
        });
        if (WaitForSingleObject(capture_done_, 3000) != WAIT_OBJECT_0) return;
        if (stop_.load(std::memory_order_acquire) || !captured_) return;
        HostScreenshot::EncodePixels(buf_, w_, h_, path_);
    }

    std::wstring            path_;
    std::vector<uint32_t>   buf_;
    uint32_t                w_ = 0, h_ = 0;
    bool                    captured_ = false;  /* UI writes, worker reads after event */
    std::thread             thread_;
    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::atomic<bool>       stop_{false};
    HANDLE                  capture_done_ = nullptr;
};

REGISTER_SERVICE(LiveScreenshotReporter);

}  // namespace
