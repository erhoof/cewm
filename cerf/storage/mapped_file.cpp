#include "mapped_file.h"

#include "../core/string_utils.h"

#include <algorithm>
#include <cstring>

MappedFile::~MappedFile() {
    if (view_)    UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
}

bool MappedFile::Open(const std::string& path) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    granularity_ = si.dwAllocationGranularity;
    if (granularity_ == 0) granularity_ = 0x10000u;       /* 64 KB fallback */
    if (window_ < granularity_) window_ = granularity_;

    file_ = CreateFileW(Utf8ToWide(path.c_str()).c_str(), GENERIC_READ,
                        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file_, &sz) || sz.QuadPart <= 0) return false;
    size_ = static_cast<uint64_t>(sz.QuadPart);

    /* Section over the whole file: a kernel object, not address space. Views
       (mapped in EnsureWindow) are bounded, so 32-bit VA is never exhausted. */
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    return mapping_ != nullptr;
}

bool MappedFile::EnsureWindow(uint64_t offset, size_t len) {
    if (offset >= size_ || len > size_ - offset) return false;
    if (view_ && offset >= view_base_ &&
        offset + len <= view_base_ + view_len_) {
        return true;                                       /* already covered */
    }

    /* View offset must be a multiple of the allocation granularity. Cover from
       the aligned base through offset+len, at least one window. */
    const uint64_t base = (offset / granularity_) * granularity_;
    const size_t   need = static_cast<size_t>(offset - base) + len;
    size_t want = std::max(window_, need);
    if (base + want > size_) want = static_cast<size_t>(size_ - base);

    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; view_len_ = 0; }
    void* p = MapViewOfFile(mapping_, FILE_MAP_READ,
                            static_cast<DWORD>(base >> 32),
                            static_cast<DWORD>(base & 0xFFFFFFFFu),
                            want);
    if (!p) return false;
    view_      = static_cast<uint8_t*>(p);
    view_base_ = base;
    view_len_  = want;
    return true;
}

const uint8_t* MappedFile::View(uint64_t offset, size_t len) {
    if (len > window_) return nullptr;                     /* exceeds a window */
    if (!EnsureWindow(offset, len)) return nullptr;
    return view_ + static_cast<size_t>(offset - view_base_);
}

size_t MappedFile::Read(uint64_t offset, void* dst, size_t len) {
    if (offset >= size_) return 0;
    len = static_cast<size_t>(std::min<uint64_t>(len, size_ - offset));

    auto*  out    = static_cast<uint8_t*>(dst);
    size_t copied = 0;
    while (copied < len) {
        const uint64_t pos   = offset + copied;
        const size_t   chunk = std::min(len - copied, window_);
        if (!EnsureWindow(pos, chunk)) break;
        const size_t avail = view_len_ - static_cast<size_t>(pos - view_base_);
        const size_t n     = std::min(chunk, avail);
        if (n == 0) break;
        std::memcpy(out + copied, view_ + static_cast<size_t>(pos - view_base_), n);
        copied += n;
    }
    return copied;
}
