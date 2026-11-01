// Interpreter implementing upstream's `ArmJit` class (include/cerfengine/arm/arm_jit.h) so CERF's own code (which names ArmJit
// directly) works unchanged. No translation cache or block compiler is used. Run() mirrors upstream ArmJit::Run's housekeeping;
// instructions run in arm_interp.cpp.
#include <cerfengine/arm/arm_jit.h>

#include <atomic>

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/host_request_channel.h>
#include <cerfengine/byte_order.h>

#include "../host_platform.h"
#include "arm_interp.h"
#ifdef CEWM_HAVE_UNICORN
#include "arm_unicorn.h"
#endif

namespace cerfengine {

CERFENGINE_REGISTER_AS(ArmJit, GuestEngine);

namespace {
ArmInterp* g_interp = nullptr;  // shortcut: one engine per process; ArmJit (upstream header) has no member to hold it
std::atomic<uint64_t> g_insn_count{0};
std::atomic<uint64_t> g_run_count{0};
constexpr uint32_t kBatchInsns = 4096;  // instructions per Run() call before housekeeping runs again
}

void ArmJit::OnReady() {
    cpu_           = &engine_.Get<ArmCpu>();
    cpu_state_     = cpu_->State();
    mmu_           = &engine_.Get<ArmMmu>();
    translator_    = &engine_.Get<ArmTranslator>();
    channel_       = &engine_.Get<ArmInterruptChannel>();
    host_requests_ = &engine_.Get<HostRequestChannel>();
    host_          = &engine_.Host();
    engine_.AttachClock(clock_);
    g_interp = new ArmInterp(engine_);
}

GuestEngine::RunStatus ArmJit::Run() {
    g_run_count.fetch_add(1, std::memory_order_relaxed);
    if (!(g_interp->DynEnabled() && g_interp->SctlrPending())) mmu_->SynchronizeSctlr();  // dyn mode applies a pending MMU change at fetch time

    if (cpu_state_->reset_pending != 0u) {
        std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request).fetch_and(~kChainExitReset, std::memory_order_acq_rel);
        host_->OnResetDelivered();
        cpu_->RaiseResetException();
        FlushTranslationCache();  // reload Unicorn CP15 after reset, on the CPU thread
        return RunStatus::kRunning;
    }

    if (channel_->TakeHostRequest()) host_requests_->ServiceRequests();
    host::ClockDispatch(clock_);

    {
        std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
        const uint32_t exits = word.load(std::memory_order_acquire);
        if ((exits & (kChainExitClock | kChainExitIdle)) != 0u) {
            if ((exits & kChainExitClock) != 0u) channel_->WaitForClock();
            else                                 channel_->ResumeIdle();
            return RunStatus::kRunning;
        }
        if ((exits & kChainExitIrq) != 0u) word.fetch_and(~kChainExitIrq, std::memory_order_acq_rel);
    }
    const ArmInterruptChannel::IrqGate gate = channel_->EvaluateGate();
    cpu_state_->irq_interrupt_pending = gate.level;
    if (gate.raise) cpu_->RaiseIrqException(cpu_state_->gprs[ArmGpr::kR15]);

#ifdef CEWM_HAVE_UNICORN
    if (auto* unicorn = engine_.TryGet<ArmUnicorn>()) {
        const int32_t left = static_cast<int32_t>(cpu_state_->guest_cycle_deadline - cpu_state_->guest_cycle_counter);
        if (left <= 0) return RunStatus::kRunning;
        auto run = [&] {
            const auto n = unicorn->Run(static_cast<uint32_t>(left < 8192 ? left : 8192));
            g_insn_count.store(g_insn_count.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
        };
        return platform::CallHaltGuarded(run) ? RunStatus::kRunning : RunStatus::kHalted;
    }
#endif
    if (g_interp->DynEnabled()) {
        const int32_t left = static_cast<int32_t>(cpu_state_->guest_cycle_deadline - cpu_state_->guest_cycle_counter);
        if (left <= 0) return RunStatus::kRunning;
        auto dyn = [&] { g_interp->RunDyn(static_cast<uint32_t>(left < 8192 ? left : 8192)); };
        if (!platform::CallHaltGuarded(dyn)) return RunStatus::kHalted;
        return RunStatus::kRunning;
    }
    auto run = [&] {
        // Cycle-deadline and exit-request checks run every 32 instructions: those words sit in ArmCpuState, shared with other threads.
        uint32_t total = 0;
        while (total < kBatchInsns && !ArmCycleDeadlineReached(*cpu_state_)) {
            uint32_t k = 0;
            bool stop = false;
            while (k < 32 && !stop) { uint32_t n; stop = g_interp->RunBlock(&n).stop_batch; k += n; }
            cpu_state_->guest_cycle_counter += k;  // shortcut: 1 cycle per instruction (upstream uses ArmInstructionTiming)
            g_insn_count.store(g_insn_count.load(std::memory_order_relaxed) + k, std::memory_order_relaxed);  // single writer: no locked add
            total += k;
            if (stop || std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request).load(std::memory_order_relaxed) != 0u) return;
        }
    };
    if (!platform::CallHaltGuarded(run)) return RunStatus::kHalted;
    return RunStatus::kRunning;
}

void ArmJit::SetHostChainExit(bool requested) {
    std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
    if (requested) word.fetch_or(kChainExitHost, std::memory_order_acq_rel);
    else           word.fetch_and(~kChainExitHost, std::memory_order_acq_rel);
}

void ArmJit::SetInterruptPending()   { channel_->SetInterruptPending(); }
void ArmJit::ClearInterruptPending() { channel_->ClearInterruptPending(); }
void ArmJit::SetIdleWake(bool level) { channel_->SetIdleWake(level); }

void ArmJit::EnterDeepSleep() { cpu_state_->deep_sleep = 1; }
void ArmJit::ExitDeepSleep() {
    cpu_state_->deep_sleep = 0;
    channel_->Wake();
}
void ArmJit::EnterIdleWait() { channel_->EnterIdle(); }

void ArmJit::SetCoreClockStopped(bool stopped) {
    channel_->SetClockStopped(stopped);
    std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
    if (stopped) word.fetch_or(kChainExitClock, std::memory_order_acq_rel);
    else         word.fetch_and(~kChainExitClock, std::memory_order_acq_rel);
}

void ArmJit::SetResetPending(bool is_resume) {
    host_->OnResetRequested(is_resume);
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request).fetch_or(kChainExitReset, std::memory_order_acq_rel);
    cpu_state_->reset_pending = 1u;
    channel_->Wake();
    if (is_resume) return;
    host_->OnRebootRequested();
}

void ArmJit::PrintFatalDump() {
    const auto& r = cpu_state_->gprs;
    CERFENGINE_LOG(engine_, kCaution, "      guest PC=0x%08X  CPSR=0x%08X\n", r[ArmGpr::kR15], ArmPackCpsr(*cpu_state_));
    CERFENGINE_LOG(engine_, kCaution, "      R0=0x%08X  R1=0x%08X  R2=0x%08X  R3=0x%08X R4=0x%08X  R5=0x%08X  R6=0x%08X  R7=0x%08X\n",
                   r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    CERFENGINE_LOG(engine_, kCaution, "      R8=0x%08X  R9=0x%08X  R10=0x%08X R11=0x%08X R12=0x%08X SP=0x%08X  LR=0x%08X\n",
                   r[8], r[9], r[10], r[11], r[12], r[13], r[14]);
}

void ArmJit::FlushTranslationCache() {
#ifdef CEWM_HAVE_UNICORN
    if (auto* unicorn = engine_.TryGet<ArmUnicorn>()) unicorn->Flush();
#endif
}
void ArmJit::SetDmaRegion(uint32_t, uint32_t) {}

std::optional<uint8_t*> ArmJit::PeekGuestVa(uint32_t va) {
    // A trace must also see mapped pages that are absent from the data TLB.
    // Probe without filling the TLB or raising a guest exception.
    if (auto* p = translator_->PeekVaToHost(va)) return p;
    return std::nullopt;
}
uint8_t* ArmJit::ResolveGuestVaToHost(uint32_t va) { return translator_->PeekVaToHost(va); }
bool ArmJit::ResolveGuestVaToPa(uint32_t va, uint32_t* pa) { return translator_->PeekVaToPa(va, pa); }

extern uint64_t g_blk_runs, g_blk_chained, g_blk_slow, g_blk_builds, g_blk_ops, g_blk_step;
void InterpBlockStats(uint64_t* o) { o[0]=g_blk_runs; o[1]=g_blk_slow; o[2]=g_blk_builds; o[3]=g_blk_ops; o[4]=g_blk_step; }
extern uint64_t g_idle_hits;
uint64_t InterpIdleHits() { return g_idle_hits; }
uint64_t InterpRunCount() { return g_run_count.load(std::memory_order_relaxed); }
uint64_t InterpInsnCount() { return g_insn_count.load(std::memory_order_relaxed); }  // diagnostics

}  // namespace cerfengine
