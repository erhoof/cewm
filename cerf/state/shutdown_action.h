#pragma once

#include "../core/service.h"

enum class ShutdownChoice;

class ShutdownAction : public Service {
public:
    using Service::Service;

    void Perform(ShutdownChoice c);
};
