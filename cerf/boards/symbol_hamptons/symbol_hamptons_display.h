#pragma once

#include "../../core/service.h"

#include <cstdint>

class SymbolHamptonsDisplay : public Service {
public:
    using Service::Service;

    virtual uint32_t DisplayId() const = 0;
};
