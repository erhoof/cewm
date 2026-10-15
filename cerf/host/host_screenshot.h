#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

/* PNG-save / clipboard-copy of a 1:1 BGRA32 guest-surface image. Save()/Copy()
   capture the main window via HostCanvas; SavePixels/CopyPixels encode an
   already-captured buffer so other windows (a PCMCIA VGA card's external
   display) reuse the same path with their own pixels. */

class HostScreenshot : public Service {
public:
    using Service::Service;

    void Save();   /* main window -> <exe_dir>\screenshots\<device>_<ts>.png */
    void Copy();   /* main window -> clipboard as CF_DIB */

    void SaveGuestSurfaceTo(const std::wstring& path);

    static bool EncodePixels(const std::vector<uint32_t>& px, uint32_t w,
                             uint32_t h, const std::wstring& path);

    /* Shared primitives over an already-captured top-down BGRA32 buffer. */
    void SavePixels(const std::vector<uint32_t>& px, uint32_t w, uint32_t h,
                    const std::wstring& name_hint);
    void CopyPixels(const std::vector<uint32_t>& px, uint32_t w, uint32_t h,
                    HWND clipboard_owner);
};
