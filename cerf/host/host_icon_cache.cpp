#define NOMINMAX
#include "host_icon_cache.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"

REGISTER_SERVICE(HostIconCache);

HostIconCache::~HostIconCache() {
    for (auto& e : cache_) DestroyIcon(e.second);
}

void HostIconCache::DrawCentered(HDC dc, const RECT& box,
                                 const wchar_t* res_name) {
    const int bw = box.right - box.left;
    const int bh = box.bottom - box.top;
    int px = (bw < bh ? bw : bh) - 4;   /* small padding inside the slot box */

    HICON h = Resolve(res_name, px);
    if (!h) {
        LOG(Caution, "HostIconCache: ICON resource '%ls' absent from cerf.exe "
            "(missing/typo'd cerf.rc entry)\n", res_name);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    DrawIconEx(dc, box.left + (bw - px) / 2, box.top + (bh - px) / 2,
               h, px, px, 0, nullptr, DI_NORMAL);
}

HICON HostIconCache::Resolve(const wchar_t* res_name, int px) {
    std::pair<std::wstring, int> key(res_name, px);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    /* comctl32's LoadIconWithScaleDown is a NONAME export, so it is reachable
       only by ordinal - and the ordinal is not stable across Windows versions.
       LoadImage scales an icon-group image to cx/cy and is exported by name
       (MS Learn LoadImageW: minimum supported client Windows 2000). */
    HICON h = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), res_name,
                                            IMAGE_ICON, px, px,
                                            LR_DEFAULTCOLOR));
    if (h) cache_[key] = h;
    return h;
}
