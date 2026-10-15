#include "../core/cerf_emulator.h"
#include "../core/cerf_paths.h"
#include "../core/device_config.h"
#include "../core/string_utils.h"
#include "host_window.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace {

class LauncherStatusReporter : public Service {
public:
    using Service::Service;

    ~LauncherStatusReporter() override { Stop(); }
    void OnShutdown() override { Stop(); }

    void OnReady() override {
        const std::string name = emu_.Get<DeviceConfig>().device_name;
        if (name.empty()) return;
        const std::string dir = GetDeviceDir(name);
        status_path_ =
            (std::filesystem::path(Utf8ToWide(dir.c_str())) / L"cerf-status.json")
                .wstring();
        started_unix_ = NowUnix();
        thread_ = std::thread([this] { HeartbeatLoop(); });
    }

private:
    static constexpr auto kHeartbeatInterval = std::chrono::seconds(2);

    static int64_t NowUnix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    void Stop() {
        stop_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> g(cv_mtx_);
            cv_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
        if (!status_path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(status_path_), ec);
        }
    }

    void HeartbeatLoop() {
        std::unique_lock<std::mutex> lk(cv_mtx_);
        while (!stop_.load(std::memory_order_acquire)) {
            lk.unlock();
            WriteStatus();
            lk.lock();
            if (stop_.load(std::memory_order_acquire)) break;
            cv_.wait_for(lk, kHeartbeatInterval);
        }
    }

    void WriteStatus() {
        const HWND hwnd = emu_.Get<HostWindow>().Hwnd();
        nlohmann::json j;
        j["pid"]            = static_cast<uint32_t>(::GetCurrentProcessId());
        j["hwnd"]           = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
        j["started_unix"]   = started_unix_;
        j["heartbeat_unix"] = NowUnix();
        j["state"]          = "running";
        AtomicWrite(j.dump());
    }

    void AtomicWrite(const std::string& text) const {
        const std::wstring tmp = status_path_ + L".tmp";
        {
            std::ofstream f(std::filesystem::path(tmp),
                            std::ios::binary | std::ios::trunc);
            if (!f) return;
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        ::MoveFileExW(tmp.c_str(), status_path_.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    std::wstring            status_path_;
    int64_t                 started_unix_ = 0;
    std::thread             thread_;
    std::mutex              cv_mtx_;
    std::condition_variable cv_;
    std::atomic<bool>       stop_{false};
};

REGISTER_SERVICE(LauncherStatusReporter);

}  // namespace
