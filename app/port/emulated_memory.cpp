// Forked from cerf/cerf/cpu/emulated_memory.cpp (MIT) with POSIX replacements for Win32 calls. See plan/README.md.
#include <sys/mman.h>
#include <cerrno>
// Our fork reaches into the class for the sparse state save (CewmCaptureFlashBaseline needs the region table).
#define private public
#include "cpu/emulated_memory.h"
#undef private

#include "core/byte_order.h"
#include "core/cerf_emulator.h"
#include "core/fatal.h"
#include "core/log.h"
#include "boards/page_table_builder.h"
#include "peripherals/peripheral_dispatcher.h"
#include "state/state_stream.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

REGISTER_SERVICE(EmulatedMemory);

void EmulatedMemory::OnReady() {
    auto& page = emu_.Get<PageTableBuilder>();
    for (const auto& r : page.BackedMemoryRegions()) {
        AddRegion(r.pa_base, r.size, r.page_protect, r.decode_span);
    }
}

void EmulatedMemory::AddRegion(uint32_t base, uint32_t size,
                               DWORD page_protect, uint32_t decode_span) {
    std::lock_guard<std::mutex> lk(add_mutex_);

    const uint32_t span = decode_span ? decode_span : size;
    if (size == 0 || span < size || uint64_t(base) + span > (uint64_t{1} << 32) ||
        span % size != 0 ||
        (span != size && (size & (size - 1u)) != 0)) {
        LOG(Caution, "EmulatedMemory::AddRegion invalid region: base=0x%08X "
                "size=0x%X span=0x%X\n", base, size, span);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (emu_.Get<PeripheralDispatcher>().OverlapsDataInversion(base, span)) {
        emu_.Get<Fatal>().Die("EmulatedMemory::AddRegion [0x%08X..0x%llX) overlaps a bus "
                              "data inversion range", base,
                              static_cast<unsigned long long>(base) + span);
    }
    const uint32_t wrap_mask = (span == size) ? 0xFFFFFFFFu : (size - 1u);

    /* count_.load(acquire) inside the writer lock - we hold the writer
       lock, so no other writer can be racing; the acquire pairs with
       earlier store_release publications. */
    const size_t n = count_.load(std::memory_order_acquire);

    for (size_t i = 0; i < n; ++i) {
        const Region& r = regions_[i];
        if (uint64_t(base) < uint64_t(r.base) + r.span &&
            uint64_t(r.base) < uint64_t(base) + span) {
            LOG(Caution, "EmulatedMemory::AddRegion overlap: new "
                    "[0x%08X..0x%llX) vs existing [0x%08X..0x%llX)\n",
                    base, static_cast<unsigned long long>(base) + span,
                    r.base, static_cast<unsigned long long>(r.base) + r.span);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }

    if (n >= kMaxRegions) {
        LOG(Caution, "EmulatedMemory::AddRegion: kMaxRegions=%zu exceeded "
                "(new [0x%08X..0x%08X)). Bump kMaxRegions in "
                "emulated_memory.h.\n",
                kMaxRegions, base, base + size);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    regions_[n].base         = base;
    regions_[n].size         = size;
    regions_[n].span         = span;
    regions_[n].wrap_mask    = wrap_mask;
    regions_[n].page_protect = page_protect;

    count_.store(n + 1, std::memory_order_release);

    LOG(Mem, "AddRegion 0x%08X size 0x%X span 0x%X protect 0x%X (slot %zu)\n",
        base, size, span, page_protect, n);
}

bool EmulatedMemory::OverlapsRegion(uint32_t base, uint32_t size) const {
    const size_t n = count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        const Region& r = regions_[i];
        if (uint64_t(base) < uint64_t(r.base) + r.span &&
            uint64_t(r.base) < uint64_t(base) + size) {
            return true;
        }
    }
    return false;
}

EmulatedMemory::Region* EmulatedMemory::FindRegion(uint32_t vaddr) {
    const size_t n = count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        Region& r = regions_[i];
        if (vaddr >= r.base && vaddr - r.base < r.span) {
            return &r;
        }
    }
    return nullptr;
}

uint8_t* EmulatedMemory::EnsureBacked(Region* r) {
    /* Fast path - already backed. memory_order_acquire so reads of the
       host page that follow this load see the VirtualAlloc memory. */
    if (uint8_t* ptr = r->host_ptr.load(std::memory_order_acquire)) {
        return ptr;
    }

    uint8_t* allocated = static_cast<uint8_t*>(::mmap(
        nullptr, r->size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    if (allocated == MAP_FAILED) {
        LOG(Caution, "EmulatedMemory::EnsureBacked mmap failed "
                "region 0x%08X size 0x%X (errno=%d)\n",
                r->base, r->size, errno);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    uint8_t* expected = nullptr;
    if (r->host_ptr.compare_exchange_strong(
            expected, allocated,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        LOG(Mem, "Region 0x%08X backed at host %p\n", r->base, allocated);
        return allocated;
    }

    /* Lost the race. expected now holds the winner's pointer. Free
       our orphan allocation and return the winner. */
    ::munmap(allocated, r->size);
    return expected;
}

void EmulatedMemory::SetAddressDecode(uint32_t region_base, const AddressDecode* decode) {
    Region* r = FindRegion(region_base);
    if (!r || r->base != region_base || r->span != r->size ||
        (r->size & (r->size - 1u)) != 0u)
        emu_.Get<Fatal>().Die("EmulatedMemory::SetAddressDecode needs a power-of-two "
                              "region at 0x%08X", region_base);
    if (decode) {
        const uint32_t backed_bits = r->size - 1u;
        bool fits = (decode->pass_mask & (PAGE_SIZE - 1u)) == PAGE_SIZE - 1u;
        for (uint32_t moved = decode->gather_mask; fits && moved != 0u; moved &= moved - 1u)
            fits = (decode->targets[std::countr_zero(moved)] & ~backed_bits) == 0u;
        if (!fits)
            emu_.Get<Fatal>().Die("EmulatedMemory::SetAddressDecode region 0x%08X size 0x%X: "
                                  "pass 0x%08X gather 0x%08X leaves the page or the region",
                                  region_base, r->size, decode->pass_mask,
                                  decode->gather_mask);
    }
    r->decode.store(decode, std::memory_order_release);
    LOG(Mem, "SetAddressDecode region 0x%08X pass 0x%08X gather 0x%08X\n", region_base,
        decode ? decode->pass_mask : 0xFFFFFFFFu, decode ? decode->gather_mask : 0u);
    for (auto& fn : decode_listeners_) fn();
}

void EmulatedMemory::RegisterDecodeListener(std::function<void()> fn) {
    decode_listeners_.push_back(std::move(fn));
}

CERF_FORCEINLINE uint8_t* EmulatedMemory::PhysicalTranslate(uint32_t pa, bool write,
                                                            cerfengine::PhysicalPage& page) {
    Region* r = FindRegion(pa);
    if (!r) return nullptr;
    const bool flash = IsFlash(*r);
    if (write && flash) return nullptr;

    uint8_t* const host    = EnsureBacked(r);
    const uint32_t page_pa = pa & ~(PAGE_SIZE - 1u);
    if (page_pa >= r->base) {
        const uint32_t offset = page_pa - r->base;
        const uint32_t backed = BackedOffset(*r, offset);
        if (RangeLinear(*r, offset, PAGE_SIZE, backed)) {
            page.read  = host + backed;
            page.write = flash ? nullptr : host + backed;
        }
    }
    return host + BackedOffset(*r, pa - r->base);
}

uint8_t* EmulatedMemory::Translate(uint32_t vaddr) {
    Region* r = FindRegion(vaddr);
    if (!r)
        emu_.Get<Fatal>().Die("EmulatedMemory::Translate unmapped 0x%08X", vaddr);
    return EnsureBacked(r) + BackedOffset(*r, vaddr - r->base);
}

uint8_t* EmulatedMemory::TryTranslate(uint32_t paddr) {
    Region* r = FindRegion(paddr);
    if (!r) return nullptr;
    return EnsureBacked(r) + BackedOffset(*r, paddr - r->base);
}

uint8_t* EmulatedMemory::TryTranslateWrite(uint32_t paddr) {
    Region* r = FindRegion(paddr);
    if (!r) {
        return nullptr;
    }

    /* PAGE_READONLY / PAGE_EXECUTE_READ → flash / ROM region. Write
       must dispatch to the flash controller / I/O peripheral instead
       of caching a host pointer; signal that to the walker by
       returning nullptr. */
    if (IsFlash(*r)) {
        return nullptr;
    }

    return EnsureBacked(r) + BackedOffset(*r, paddr - r->base);
}

bool EmulatedMemory::IsFlash(const Region& r) {
    return r.page_protect == PAGE_READONLY || r.page_protect == PAGE_EXECUTE_READ;
}

EmulatedMemory::Region* EmulatedMemory::BulkRegion(uint32_t vaddr, size_t size) {
    Region* r = FindRegion(vaddr);
    if (!r || static_cast<uint64_t>(vaddr) + size >
                  static_cast<uint64_t>(r->base) + r->size) {
        return nullptr;
    }
    return r;
}

bool EmulatedMemory::CanCopyRange(uint32_t paddr, size_t size, bool writable) {
    const Region* r = BulkRegion(paddr, size);
    return r && (!writable || !IsFlash(*r));
}

uint8_t* EmulatedMemory::TryTranslateRange(uint64_t paddr, uint64_t size, bool write) {
    if (size == 0 || paddr > UINT32_MAX || size > (uint64_t{1} << 32) - paddr)
        return nullptr;
    Region* r = FindRegion(static_cast<uint32_t>(paddr));
    if (!r || (write && IsFlash(*r)))
        return nullptr;
    const uint64_t offset = paddr - r->base;
    const uint64_t backed_offset = BackedOffset(*r, static_cast<uint32_t>(offset));
    if (!RangeLinear(*r, offset, size, backed_offset))
        return nullptr;
    return EnsureBacked(r) + static_cast<size_t>(backed_offset);
}

CERF_FORCEINLINE bool EmulatedMemory::PhysicalSpanUniform(uint32_t pa, uint32_t span_bytes) {
    if (span_bytes <= 0x1000u) return true;
    const uint32_t base = pa & ~(span_bytes - 1u);
    Region* r = FindRegion(base);
    if (!r) return true;
    if (r->wrap_mask != 0xFFFFFFFFu) return false;
    const uint64_t block = DecodeBlock(*r);
    const uint64_t offset = base - r->base;
    if (offset / block != (offset + span_bytes - 1u) / block) return false;
    return base >= r->base &&
           static_cast<uint64_t>(base) + span_bytes <=
               static_cast<uint64_t>(r->base) + r->size;
}

uint8_t* CERFENGINE_FASTCALL cerfengine::host::PhysicalTranslate(void* context, uint32_t pa,
                                                                 bool write,
                                                                 PhysicalPage& page) {
    return static_cast<EmulatedMemory*>(context)->PhysicalTranslate(pa, write, page);
}

bool CERFENGINE_FASTCALL cerfengine::host::PhysicalSpanUniform(void* context, uint32_t pa,
                                                               uint32_t span) {
    return static_cast<EmulatedMemory*>(context)->PhysicalSpanUniform(pa, span);
}

uint8_t EmulatedMemory::ReadByte(uint32_t vaddr) {
    return *Translate(vaddr);
}

uint16_t EmulatedMemory::ReadHalf(uint32_t vaddr) {
    return cerf::le::U16(Translate(vaddr));
}

uint32_t EmulatedMemory::ReadWord(uint32_t vaddr) {
    return cerf::le::U32(Translate(vaddr));
}

uint64_t EmulatedMemory::ReadDword(uint32_t vaddr) {
    return cerf::le::U64(Translate(vaddr));
}

void EmulatedMemory::WriteByte(uint32_t vaddr, uint8_t value) {
    *Translate(vaddr) = value;
}

void EmulatedMemory::WriteHalf(uint32_t vaddr, uint16_t value) {
    cerf::le::Put16(Translate(vaddr), value);
}

void EmulatedMemory::WriteWord(uint32_t vaddr, uint32_t value) {
    cerf::le::Put32(Translate(vaddr), value);
}

void EmulatedMemory::WriteDword(uint32_t vaddr, uint64_t value) {
    cerf::le::Put64(Translate(vaddr), value);
}

EmulatedMemory::Region* EmulatedMemory::BulkRegionFor(uint32_t vaddr,
                                                      size_t size,
                                                      const char* op) {
    if (Region* ok = BulkRegion(vaddr, size)) return ok;
    Region* r = FindRegion(vaddr);
    if (!r) {
        LOG(Caution, "EmulatedMemory::%s unmapped address "
                "0x%08X size 0x%zX\n", op, vaddr, size);
    } else {
        LOG(Caution, "EmulatedMemory::%s crosses region boundary at "
                "0x%08X size 0x%zX (region 0x%08X size 0x%X)\n",
                op, vaddr, size, r->base, r->size);
    }
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void EmulatedMemory::CopyIn(uint32_t vaddr, const void* host_src, size_t size) {
    Region* r = BulkRegionFor(vaddr, size, "CopyIn");
    uint8_t* host = EnsureBacked(r);
    const uint8_t* src = static_cast<const uint8_t*>(host_src);
    const uint64_t block = DecodeBlock(*r);
    for (size_t done = 0; done < size;) {
        const uint32_t offset = vaddr - r->base + static_cast<uint32_t>(done);
        const size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(size - done, block - offset % block));
        std::memcpy(host + BackedOffset(*r, offset), src + done, chunk);
        done += chunk;
    }
}

void EmulatedMemory::CopyOut(uint32_t vaddr, void* host_dst, size_t size) {
    Region* r = BulkRegionFor(vaddr, size, "CopyOut");
    const uint8_t* host = EnsureBacked(r);
    uint8_t* dst = static_cast<uint8_t*>(host_dst);
    const uint64_t block = DecodeBlock(*r);
    for (size_t done = 0; done < size;) {
        const uint32_t offset = vaddr - r->base + static_cast<uint32_t>(done);
        const size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(size - done, block - offset % block));
        std::memcpy(dst + done, host + BackedOffset(*r, offset), chunk);
        done += chunk;
    }
}

uint32_t EmulatedMemory::TryCopyOut(uint32_t paddr, uint8_t* host_dst, uint32_t size) {
    if (size == 0u) return 0u;
    if (const uint8_t* span = TryTranslateRange(paddr, size)) {
        std::memcpy(host_dst, span, size);
        return size;
    }
    uint32_t k = 0;
    for (; k < size; ++k) {
        const uint8_t* byte = TryTranslate(paddr + k);
        if (byte == nullptr) break;
        host_dst[k] = *byte;
    }
    return k;
}

uint32_t EmulatedMemory::TryCopyIn(uint32_t paddr, const uint8_t* host_src, uint32_t size) {
    if (size == 0u) return 0u;
    if (uint8_t* span = TryTranslateRange(paddr, size, true)) {
        std::memcpy(span, host_src, size);
        return size;
    }
    uint32_t k = 0;
    for (; k < size; ++k) {
        uint8_t* byte = TryTranslateWrite(paddr + k);
        if (byte == nullptr) break;
        *byte = host_src[k];
    }
    return k;
}

uint32_t EmulatedMemory::WritableLength(uint32_t paddr, uint32_t size) {
    if (size == 0u) return 0u;
    if (TryTranslateRange(paddr, size, true) != nullptr) return size;
    uint32_t k = 0;
    while (k < size && TryTranslateWrite(paddr + k) != nullptr) ++k;
    return k;
}

void EmulatedMemory::WipeVolatileRegions() {
    const size_t n = count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        Region& r = regions_[i];
        if (r.page_protect == PAGE_READONLY ||
            r.page_protect == PAGE_EXECUTE_READ) {
            continue;
        }
        /* Unbacked regions are already zero on first touch. */
        if (uint8_t* host = r.host_ptr.load(std::memory_order_acquire)) {
            std::memset(host, 0, r.size);
            LOG(Mem, "WipeVolatileRegions: region 0x%08X size 0x%X zeroed\n",
                r.base, r.size);
        }
    }
}

void EmulatedMemory::SaveState(StateWriter& w) { SaveRegions(w, false); }

void EmulatedMemory::SaveFlashRegions(StateWriter& w) { SaveRegions(w, true); }

void EmulatedMemory::RestoreState(StateReader& r) { RestoreRegions(r, false); }

void EmulatedMemory::RestoreFlashRegions(StateReader& r) { RestoreRegions(r, true); }

uint32_t EmulatedMemory::RegionCount(bool flash) const {
    const size_t n = count_.load(std::memory_order_acquire);
    uint32_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (IsFlash(regions_[i]) == flash) ++count;
    }
    return count;
}

// State files keep only the pages that differ from the baseline: RAM against zero, flash against the ROM image as placed at boot
// (CewmCaptureFlashBaseline hashes it once). A booted machine is mostly zero RAM plus an untouched 96 MB ROM, so this is the
// difference between ~370 MB and tens of MB.
namespace {
constexpr uint32_t kPage = 4096;
std::unordered_map<uint32_t, std::vector<uint64_t>> g_flash_base;  // region base -> hash per page

uint64_t PageHash(const uint8_t* p) {
    uint64_t h = 0x9E3779B97F4A7C15ull;
    for (uint32_t i = 0; i < kPage; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = (h ^ v) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    return h;
}
bool PageZero(const uint8_t* p) {
    static const uint8_t zero[kPage] = {};
    return memcmp(p, zero, kPage) == 0;
}
}  // namespace

void CewmCaptureFlashBaseline(EmulatedMemory& m) {
    g_flash_base.clear();
    const size_t n = m.count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        EmulatedMemory::Region& r = m.regions_[i];
        uint8_t* host = r.host_ptr.load(std::memory_order_acquire);
        if (!EmulatedMemory::IsFlash(r) || !host) continue;
        std::vector<uint64_t>& v = g_flash_base[r.base];
        v.resize(r.size / kPage);
        for (uint32_t pg = 0; pg < v.size(); ++pg) v[pg] = PageHash(host + size_t(pg) * kPage);
    }
}

void EmulatedMemory::SaveRegions(StateWriter& w, bool flash) {
    const size_t n = count_.load(std::memory_order_acquire);
    w.Write(flash ? "flash_count" : "volatile_count", RegionCount(flash));
    for (size_t i = 0; i < n; ++i) {
        Region& r = regions_[i];
        if (IsFlash(r) != flash) continue;
        w.Write("base", r.base);
        w.Write("region_size", r.size);
        uint8_t* host = r.host_ptr.load(std::memory_order_acquire);
        const uint8_t backed = host ? 1u : 0u;
        w.Write("backed", backed);
        if (!host) continue;
        const std::vector<uint64_t>* base = nullptr;
        if (flash) {
            auto it = g_flash_base.find(r.base);
            if (it == g_flash_base.end()) { LOG(Caution, "EmulatedMemory: no flash baseline for 0x%08X\n", r.base); CerfFatalExit(CERF_FATAL_RUNTIME_ERROR); }
            base = &it->second;
        }
        const uint32_t pages = r.size / kPage;
        std::vector<uint32_t> keep;
        for (uint32_t pg = 0; pg < pages; ++pg) {
            const uint8_t* p = host + size_t(pg) * kPage;
            if (flash ? PageHash(p) != (*base)[pg] : !PageZero(p)) keep.push_back(pg);
        }
        LOG(Cerf, "[HIBERNATE] %s region 0x%08X: %zu of %u pages kept\n", flash ? "flash" : "RAM", r.base, keep.size(), pages);
        w.Write<uint32_t>("pages", static_cast<uint32_t>(keep.size()));
        for (uint32_t pg : keep) {
            w.Write("page", pg);
            const uint8_t* p = host + size_t(pg) * kPage;
            if (flash) { w.WriteBytes("data", p, kPage); continue; }
            // RAM pages are mostly zero with a few touched bytes: keep only the nonzero 64-byte blocks.
            uint64_t mask = 0;
            uint8_t packed[kPage];
            size_t used = 0;
            for (uint32_t b = 0; b < 64; ++b) {
                static const uint8_t z[64] = {};
                if (memcmp(p + b * 64, z, 64) == 0) continue;
                mask |= 1ull << b;
                memcpy(packed + used, p + b * 64, 64);
                used += 64;
            }
            w.Write("mask", mask);
            w.WriteBytes("blocks", packed, used);
        }
    }
}

void EmulatedMemory::RestoreRegions(StateReader& r, bool flash) {
    const char* kind = flash ? "flash" : "RAM";
    uint32_t count = 0;
    r.Read(flash ? "flash_count" : "volatile_count", count);
    if (count != RegionCount(flash))
        r.Reject("the image has %u %s regions, this build has %u", count, kind, RegionCount(flash));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t base = 0, size = 0;
        r.Read("base", base);
        r.Read("region_size", size);
        Region* reg = FindRegion(base);
        if (!reg || reg->base != base || reg->size != size || IsFlash(*reg) != flash)
            r.Reject("%s region base=0x%08X size=0x%X is not in this memory map", kind, base, size);
        uint8_t backed = 0;
        r.Read("backed", backed);
        if (!backed) continue;
        uint32_t saved_pages = 0;
        r.Read("pages", saved_pages);
        const uint32_t pages = size / kPage;
        uint8_t* host = flash ? EnsureBacked(reg) : reg->host_ptr.load(std::memory_order_acquire);
        // The machine is at its boot state: pages the saved machine did not keep must return to the baseline (flash: the ROM, still in
        // place; RAM: zero). RAM was only touched where the boot code placed data, so scanning never commits untouched memory.
        std::vector<uint8_t> keep(pages, 0);
        for (uint32_t i = 0; i < saved_pages; ++i) {
            uint32_t pg = 0;
            r.Read("page", pg);
            if (pg >= pages) r.Reject("page %u is outside the %s region", pg, kind);
            if (!host) host = EnsureBacked(reg);
            uint8_t* p = host + size_t(pg) * kPage;
            if (flash) {
                r.ReadBytes("data", p, kPage);
            } else {
                uint64_t mask = 0;
                uint8_t packed[kPage];
                r.Read("mask", mask);
                const size_t used = size_t(__builtin_popcountll(mask)) * 64;
                r.ReadBytes("blocks", packed, used);
                memset(p, 0, kPage);
                for (uint32_t b = 0, at = 0; b < 64; ++b)
                    if (mask >> b & 1) { memcpy(p + b * 64, packed + at, 64); at += 64; }
            }
            keep[pg] = 1;
        }
        if (host && !flash)
            for (uint32_t pg = 0; pg < pages; ++pg)
                if (!keep[pg] && !PageZero(host + size_t(pg) * kPage)) memset(host + size_t(pg) * kPage, 0, kPage);
        // Flash pages the saved machine did not keep are unchanged from the ROM and so are still in place only if the guest has not
        // written them since boot; at restore time it has not run yet, so nothing to do.
    }
}

uint64_t EmulatedMemory::VolatileByteCount() const {
    const size_t n = count_.load(std::memory_order_acquire);
    uint64_t total = 0;
    for (size_t i = 0; i < n; ++i) {
        const Region& r = regions_[i];
        if (r.page_protect == PAGE_READONLY || r.page_protect == PAGE_EXECUTE_READ)
            continue;
        total += r.size;
    }
    return total;
}
