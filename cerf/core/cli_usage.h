#pragma once

#include "service.h"

class CliUsage : public Service {
public:
    using Service::Service;

    void Print(const char* prog);
};
