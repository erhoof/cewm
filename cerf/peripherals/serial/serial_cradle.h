#pragma once

#include "../../host/host_widget.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CerfEmulator;
class SerialLine;
class SerialEndpoint;
class StateWriter;
class StateReader;

class SerialCradle : public HostWidget {
public:
    SerialCradle(CerfEmulator& emu, SerialLine& line, std::wstring label);
    ~SerialCradle();

    void OnShutdown();

    void SaveCradleState(StateWriter& w);
    void RestoreCradleState(StateReader& r);
    void PostRestore();

    std::wstring WidgetName() const override { return label_; }
    WidgetGroup  Group() const override { return WidgetGroup::Network; }
    std::wstring Tooltip() const override;
    std::vector<WidgetMenuItem> BuildMenu() override;
    void DrawIcon(HDC dc, const RECT& box) const override;
    bool PollDirty() override;

private:
    enum class Kind { None, Modem, Forward };

    void SwapModem(uint64_t gen);
    void SwapForward(uint64_t gen, std::wstring host_port);
    void Eject(uint64_t gen);
    void SetPluggedLocked(Kind kind, std::wstring host_port);
    std::vector<WidgetMenuItem> BuildInsertSubmenuLocked(uint64_t gen);
    const wchar_t* IconResourceLocked() const;

    CerfEmulator& emu_;
    SerialLine&   line_;
    std::wstring  label_;

    mutable std::mutex mtx_;
    Kind                            kind_ = Kind::None;
    std::wstring                    host_port_;
    std::unique_ptr<SerialEndpoint> endpoint_;
    uint64_t                        generation_ = 0;

    Kind         restored_kind_ = Kind::None;
    std::wstring restored_port_;

    std::wstring ui_last_res_;
};
