#pragma once

#include "service.h"

class DeviceNotFoundService : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void EnsureFound();

private:
    bool IsDevicePresent();
};
