#pragma once

#include "../core/service.h"

#include <atomic>

class PointerSource;

class GuestCalibrationWarning : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void OnCalibrationAppeared();
    void OnCalibrationDisappeared();

private:
    void ShowWarning();
    void EndCycle();
    PointerSource* StockSource();

    std::atomic<bool> present_{false};
    bool              switched_to_stock_ = false;
};
