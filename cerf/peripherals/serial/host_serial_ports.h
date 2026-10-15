#pragma once

#include "../../core/service.h"

#include <string>
#include <vector>

class HostSerialPorts : public Service {
public:
    using Service::Service;

    std::vector<std::wstring> Enumerate() const;
};
