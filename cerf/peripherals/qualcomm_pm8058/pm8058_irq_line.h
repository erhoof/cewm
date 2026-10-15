#pragma once

#include "../../core/service.h"

class Pm8058IrqLine : public Service {
public:
    using Service::Service;

    virtual void SetPm8058IrqAsserted(bool asserted) = 0;
};
