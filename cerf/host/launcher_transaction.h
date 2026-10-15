#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <nlohmann/json.hpp>

#include <string>

class LauncherTransaction : public Service {
public:
    using Service::Service;

    bool Run(HWND owner, const char* scaffolding,
             const nlohmann::json& query, nlohmann::json& response);

private:
    bool LocateLauncher(HWND owner, std::wstring& exe);
    bool WriteRequest(HWND owner, const std::string& path,
                      const nlohmann::json& request);
    bool Spawn(HWND owner, const std::wstring& exe, const std::string& device,
               const std::string& file);
    bool ReadResponse(HWND owner, const std::string& path, const char* key,
                      nlohmann::json& response);
    void PumpUntilExit(HANDLE process);
    void Complain(HWND owner, const std::wstring& text);

    bool     running_ = false;
    uint32_t serial_  = 0;
};
