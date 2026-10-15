#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"

#include <mutex>

class NecMobilepro900SeriesLed : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetBlinking(bool blinking);
    bool Blinking() const;

    std::wstring WidgetName() const override { return L"Notification LED"; }
    WidgetGroup  Group() const override { return WidgetGroup::Indicator; }
    std::wstring Tooltip() const override;
    void         DrawIcon(HDC dc, const RECT& box) const override;
    bool         PollDirty() override;

private:
    bool LitNow() const;

    mutable std::mutex mtx_;
    bool               blinking_ = false;

    bool drawn_blinking_ = false;
    bool drawn_lit_      = false;
};
