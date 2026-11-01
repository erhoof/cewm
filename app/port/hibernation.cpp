// Trimmed fork of cerf/cerf/state/hibernation.cpp (MIT): no UI (notifications, screenshots, canvas resize), no async worker, no rollback image.
// Save runs with the guest stopped (EmuStop); Restore runs from the pre-start hook, before the first guest instruction.
// State file = cewm::Host().state_path. A header mismatch (other ROM / peripheral set) refuses before touching anything;
// a mid-apply refusal deletes the file and dies, so the next launch cold-boots.
#include "state/hibernation.h"
#include "state/emulation_freeze.h"
#include "state/state_image_format.h"
#include "state/state_stream.h"

#include "boot/guest_cold_boot.h"
#include "boot/rom_parser_service.h"
#include "core/cerf_emulator.h"
#include "core/device_config.h"
#include "core/fatal.h"
#include "core/log.h"
#include "cpu/emulated_memory.h"
#include "execution/engine_bridge.h"
#include "host_state.h"
#include "peripherals/peripheral_base.h"
#include "peripherals/peripheral_dispatcher.h"
#include "socs/guest_cpu_reset.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <algorithm>

namespace {
// Presentation and Widget sections are not saved: the frame is redrawn by the guest, the battery follows host state.
constexpr StateSection kOrder[] = {StateSection::Cpu, StateSection::Mmu, StateSection::Ram, StateSection::Flash, StateSection::Periph, StateSection::Reset};
constexpr uint32_t kNotificationLedBase = 0x500FFFD0u;
uint32_t LayoutSig(const std::vector<Peripheral*>& periphs) {
    uint32_t sig = static_cast<uint32_t>(periphs.size());
    for (const Peripheral* p : periphs) sig = sig * 2654435761u + p->MmioBase();
    return sig;
}
std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
}  // namespace

REGISTER_SERVICE(Hibernation);
Hibernation::~Hibernation() = default;
void Hibernation::OnReady() {}
void Hibernation::OnShutdown() {}
void Hibernation::RegisterPreSaveListener(std::function<void()> fn) { pre_save_listeners_.push_back(std::move(fn)); }
std::wstring Hibernation::DefaultStatePath() const { return Wide(cewm::Host().state_path); }
bool Hibernation::DefaultStateExists() const {
    struct stat st;
    return !cewm::Host().state_path.empty() && stat(cewm::Host().state_path.c_str(), &st) == 0 && st.st_size > 0;
}

uint32_t Hibernation::PeripheralLayoutSig() const {
    return LayoutSig(emu_.Get<PeripheralDispatcher>().RegisteredPeripherals());
}

StateImageHeader Hibernation::LiveHeader() const {
    auto* rom = emu_.TryGet<RomParserService>();
    StateImageHeader h{};
    h.rom_entry_va = (rom && rom->Ok()) ? rom->Primary().entry_va : 0;
    h.periph_layout_sig = PeripheralLayoutSig();
    uint64_t total = 0;
    if (rom) for (const auto& p : rom->Loaded()) total += p.raw.size();
    h.rom_total_bytes = total;
    h.guest_additions = emu_.Get<DeviceConfig>().guest_additions ? 1u : 0u;
    return h;
}

void Hibernation::SaveSection(StateWriter& w, StateSection section) {
    switch (section) {
        case StateSection::Cpu: emu_.Get<EngineBridge>().SaveCpuState(w); break;
        case StateSection::Mmu: emu_.Get<EngineBridge>().SaveMmuState(w); break;
        case StateSection::Ram: emu_.Get<EmulatedMemory>().SaveState(w); break;
        case StateSection::Flash: emu_.Get<EmulatedMemory>().SaveFlashRegions(w); break;
        case StateSection::Periph: {
            const auto periphs = emu_.Get<PeripheralDispatcher>().RegisteredPeripherals();
            w.Write<uint32_t>("periph_count", static_cast<uint32_t>(periphs.size()));
            for (Peripheral* p : periphs) { w.BeginFrame(p->MmioBase()); p->SaveState(w); w.EndFrame(); }
            break;
        }
        case StateSection::Reset:
            emu_.Get<GuestCpuReset>().SaveState(w);
            emu_.Get<GuestColdBoot>().SaveState(w);
            break;
        default: break;
    }
}

bool Hibernation::Save(const std::wstring& path_in) {
    const std::wstring path = path_in.empty() ? DefaultStatePath() : path_in;
    if (path.empty()) return false;
    for (auto& fn : pre_save_listeners_) fn();
    auto snap = emu_.Get<EmulationFreeze>().SnapshotSection();
    StateWriter w(path);
    if (!w.Ok()) { LOG(Caution, "Hibernation: %s\n", w.Error().c_str()); return false; }
    w.WriteRaw(kStateMagic, sizeof(kStateMagic));
    const StateImageHeader h = LiveHeader();
    w.Write("rom_entry_va", h.rom_entry_va);
    w.Write("periph_layout_sig", h.periph_layout_sig);
    w.Write("rom_total_bytes", h.rom_total_bytes);
    w.Write("guest_additions", h.guest_additions);
    for (const StateSection section : kOrder) { w.BeginFrame(static_cast<uint32_t>(section)); SaveSection(w, section); w.EndFrame(); }
    const bool ok = w.Ok() && w.Commit();
    if (!ok) LOG(Caution, "Hibernation: save failed: %s\n", w.Error().c_str());
    else LOG(Cerf, "[HIBERNATE] state saved\n");
    return ok;
}

void Hibernation::ReadHeader(StateReader& r) {
    char magic[sizeof(kStateMagic)] = {};
    r.ReadRaw(magic, sizeof(magic));
    if (std::memcmp(magic, kStateMagic, sizeof(magic)) != 0) throw StateImageRejected("not a CERF state image (or another format version)");
    StateImageHeader saved{};
    r.Read("rom_entry_va", saved.rom_entry_va);
    r.Read("periph_layout_sig", saved.periph_layout_sig);
    r.Read("rom_total_bytes", saved.rom_total_bytes);
    r.Read("guest_additions", saved.guest_additions);
    const StateImageHeader live = LiveHeader();
    if (saved.rom_entry_va != live.rom_entry_va || saved.rom_total_bytes != live.rom_total_bytes) throw StateImageRejected("the image is for a different ROM");
    if (saved.periph_layout_sig != live.periph_layout_sig) {
        // Older CEWM builds omitted the notification LED; keep their saved machines usable.
        auto legacy = emu_.Get<PeripheralDispatcher>().RegisteredPeripherals();
        std::erase_if(legacy, [](Peripheral* p) { return p->MmioBase() == kNotificationLedBase; });
        if (saved.periph_layout_sig != LayoutSig(legacy)) throw StateImageRejected("the image has a different peripheral set");
    }
    if (saved.guest_additions != live.guest_additions) throw StateImageRejected("guest additions differ");
}

void Hibernation::RestorePeripherals(StateReader& r) {
    const auto periphs = emu_.Get<PeripheralDispatcher>().RegisteredPeripherals();
    uint32_t n = 0;
    r.Read("periph_count", n);
    const bool legacy_led = n + 1u == periphs.size() &&
        std::any_of(periphs.begin(), periphs.end(), [](Peripheral* p) { return p->MmioBase() == kNotificationLedBase; });
    if (n != periphs.size() && !legacy_led) r.Reject("the image has %u peripherals, this build has %zu", n, periphs.size());
    for (Peripheral* p : periphs) {
        if (legacy_led && p->MmioBase() == kNotificationLedBase) continue;
        const uint32_t base = r.EnterFrame();
        if (base != p->MmioBase()) r.Reject("the image has peripheral 0x%08X where this build has 0x%08X", base, p->MmioBase());
        p->RestoreState(r);
        r.LeaveFrame();
    }
    for (Peripheral* p : periphs) p->PostRestore();
}

void Hibernation::RestoreSection(StateReader& r, StateSection section) {
    switch (section) {
        case StateSection::Cpu: emu_.Get<EngineBridge>().RestoreCpuState(r); break;
        case StateSection::Mmu: emu_.Get<EngineBridge>().RestoreMmuState(r); break;
        case StateSection::Ram: emu_.Get<EmulatedMemory>().RestoreState(r); break;
        case StateSection::Flash: emu_.Get<EmulatedMemory>().RestoreFlashRegions(r); break;
        case StateSection::Periph: RestorePeripherals(r); break;
        case StateSection::Reset:
            emu_.Get<GuestCpuReset>().RestoreState(r);
            emu_.Get<GuestColdBoot>().RestoreState(r);
            break;
        default: break;
    }
}

bool Hibernation::Restore(const std::wstring& path_in, bool) {
    const std::wstring path = path_in.empty() ? DefaultStatePath() : path_in;
    StateReader r(path);
    if (!r.Ok()) { LOG(Caution, "Hibernation: %s\n", r.Error().c_str()); return false; }
    try {
        ReadHeader(r);
    } catch (const StateImageRejected& e) {
        LOG(Cerf, "[HIBERNATE] state ignored: %s\n", e.what());
        return false;
    }
    auto snap = emu_.Get<EmulationFreeze>().SnapshotSection();
    try {
        for (const StateSection section : kOrder) {
            const uint32_t id = r.EnterFrame();
            if (id != static_cast<uint32_t>(section)) r.Reject("section %u where this build reads section %u", id, static_cast<uint32_t>(section));
            RestoreSection(r, section);
            r.LeaveFrame();
        }
        if (r.Remaining() != 0) r.Reject("the image carries %llu bytes past its last section", static_cast<unsigned long long>(r.Remaining()));
    } catch (const StateImageRejected& e) {
        unlink(cewm::Host().state_path.c_str());  // half-applied machine: cold-boot next time
        emu_.Get<Fatal>().Die("Hibernation: state image refused mid-restore (deleted): %s", e.what());
    }
    emu_.Get<EngineBridge>().Guest().FlushTranslationCache();
    LOG(Cerf, "[HIBERNATE] state restored\n");
    return true;
}
