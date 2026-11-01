#pragma once
#include <cerfengine/engine.h>
#include <memory>

namespace cerfengine {
class ArmUnicorn : public EngineService {
public:
    explicit ArmUnicorn(Engine& engine);
    ~ArmUnicorn() override;
    bool ShouldRegister() override;
    uint32_t Run(uint32_t budget);
    void Flush();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool reload_ = true;
};
}
