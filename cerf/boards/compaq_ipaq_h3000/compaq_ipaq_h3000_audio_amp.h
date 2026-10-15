#pragma once

#include "../../core/service.h"

class CompaqIpaqH3000AudioAmp : public Service {
public:
    using Service::Service;

    virtual bool Powered() const = 0;
};
