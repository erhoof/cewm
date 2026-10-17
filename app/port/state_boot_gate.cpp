// Replaces upstream state_boot_gate.cpp: restore the saved state when cewm::Host().resume is set and a state file exists.
#include "state/state_boot_gate.h"

#include "core/cerf_emulator.h"
#include "cpu/emulated_memory.h"
#include "execution/jit_runner.h"
#include "host_state.h"
#include "state/hibernation.h"

void CewmCaptureFlashBaseline(EmulatedMemory&);  // port/emulated_memory.cpp

REGISTER_SERVICE(StateBootGate);
void StateBootGate::OnReady() { emu_.Get<JitRunner>().RegisterPreStartHook([this] { Run(); }); }
void StateBootGate::Run() {
    CewmCaptureFlashBaseline(emu_.Get<EmulatedMemory>());  // pristine ROM pages, for the sparse state file
    auto& hib = emu_.Get<Hibernation>();
    if (cewm::Host().resume && hib.DefaultStateExists()) hib.Restore(L"", false);
}
