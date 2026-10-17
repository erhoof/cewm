#pragma once

#include "core/service.h"

#include <cstdint>

class Sa1111SacHostInput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void SetRate(uint64_t fs_num, uint64_t fs_den);
    void Take(uint8_t* dst, uint32_t words);
    void Clear();

private:
    uint32_t   rate_ = 0;
};
