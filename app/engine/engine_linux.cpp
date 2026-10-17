// Derived from libCerfEngine/src/engine.cpp (MIT): ARM-only, no MIPS, no x86 JIT backend. See plan/README.md.
#include <cerfengine/engine.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "host_platform.h"


namespace cerfengine {

namespace {

constexpr size_t kMessageBytes = 2048;

}

Engine::Engine(const Setup& setup) : setup_(setup) {}

void Engine::BindClock(ClockHost& clock) {
    clock_ = &clock;
    for (ClockHost** slot : clock_slots_) *slot = clock_;
}

void Engine::AttachClock(ClockHost*& slot) {
    clock_slots_.push_back(&slot);
    slot = clock_;
}

Engine::~Engine() {
    TearDown();
}

bool Engine::ValidateSetup(std::string& error) {
    if (setup_.host == nullptr || setup_.mmio.generation == nullptr) {
        error = "the setup needs a host and the MMIO bus generation counter";
        return false;
    }
    if (setup_.isa != Isa::kArm || setup_.arm_system == nullptr) {
        error = "this build supports ARM only and needs an ARM system host";
        return false;
    }
    if (setup_.host->CachedDramRegions().empty()) {
        error = "the host declares no cached DRAM region";
        return false;
    }
    return true;
}

bool Engine::Start(std::string& error) {
    if (!ValidateSetup(error)) return false;
    Bootstrap();
    return true;
}

void Engine::Log(LogChannel channel, const char* fmt, ...) {
    char text[kMessageBytes];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Host().Log(channel, text);
}

void Engine::Panic(const char* fmt, ...) {
    char text[kMessageBytes];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Host().Panic(text);
    std::abort();
}

void Engine::Halt(HaltReason reason, const char* fmt, ...) {
    halt_.reason = reason;
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(halt_.detail, sizeof(halt_.detail), fmt, args);
    va_end(args);
    platform::RaiseUnwind(platform::kHaltUnwindCode);
}

void Engine::OnLocatorError(const char* message) {
    if (setup_.host != nullptr) setup_.host->Panic(message);
    std::fprintf(stderr, "cerfengine: %s\n", message);
    std::fflush(stderr);
    std::abort();
}

}
