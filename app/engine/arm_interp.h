// ARM/Thumb interpreter core. Decoding is upstream's (ArmDecoder/ThumbDecoder produce a DecodedInsn whose place_fn identifies the
// operation); loads/stores go through upstream's ArmRoutedInstruction::Complete, which already executes them in C++.
#pragma once
#include <cerfengine/engine.h>

namespace cerfengine {

class ArmCpu;
struct ArmCpuState;

struct InterpStep {
    bool stop_batch = false;  // leave the Run() batch (WFI idle, mode change needing housekeeping)
};

class ArmInterp {
public:
    explicit ArmInterp(Engine& engine);
    InterpStep Step();
    // Runs one cached block of pre-decoded instructions; *executed = how many instructions retired (at least 1).
    InterpStep RunBlock(uint32_t* executed);

    // Dynarmic-backed fast path (CEWM_JIT=1): runs translated code for up to `budget` guest cycles; privileged instructions, faults and
    // exceptions are handled by the interpreter core. Returns when the budget is spent or something needs Run()'s housekeeping.
    bool DynEnabled() const;
    bool SctlrPending() const;  // an MMU-control write is waiting for the next out-of-window fetch (dyn mode)
    void RunDyn(uint32_t budget);

private:
    friend struct DynCore;
    struct Impl;
    Impl* impl_;
};

}  // namespace cerfengine
