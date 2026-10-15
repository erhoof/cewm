#pragma once

#include "../../core/service.h"

#include <cstdint>
#include <string>
#include <vector>

class CompactFlashFat16Builder : public Service {
public:
    using Service::Service;

    bool Build(const std::wstring& out_path,
               const std::vector<std::wstring>& files,
               uint32_t data_mb);
};
