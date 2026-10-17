// Linux replacement for libCerfEngine's host_platform_win32.cpp (interpreter only: no SEH, no JIT faults).
#include "host_platform.h"

#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

namespace cerfengine::platform {

namespace {
struct HaltUnwind {};
int SignalFd(void* handle) {
    const uint64_t one = 1;
    return write(static_cast<int>(reinterpret_cast<intptr_t>(handle)), &one, sizeof one) == sizeof one;
}
}  // namespace

void* AllocateExecutable(size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

void FreeExecutable(void* base) {
    (void)base;  // shortcut: size unknown here, interpreter never allocates executable memory
}

// Interpreter frames are ordinary C++ frames, so a C++ exception unwinds fine.
void RaiseUnwind(uint32_t) { throw HaltUnwind{}; }

bool CallHaltGuarded(void (*fn)(void* context), void* context) {
    try {
        fn(context);
    } catch (const HaltUnwind&) {
        return false;
    }
    return true;
}

WakeEvent CreateWakeEvent() {
    WakeEvent e;
    e.handle = reinterpret_cast<void*>(static_cast<intptr_t>(eventfd(0, EFD_CLOEXEC)));
    e.signal = &SignalFd;
    return e;
}

void CloseWakeEvent(const WakeEvent& e) {
    if (e.handle != nullptr) close(static_cast<int>(reinterpret_cast<intptr_t>(e.handle)));
}

uint32_t LastErrorCode() { return static_cast<uint32_t>(errno); }

int CaptureFault(void*, HostFault&) { return 0; }  // JIT-only

}  // namespace cerfengine::platform
