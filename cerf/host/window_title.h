#pragma once

#include "../core/service.h"

#include <string>

class WindowTitle : public Service {
public:
    using Service::Service;

    std::wstring Compose() const;
};
