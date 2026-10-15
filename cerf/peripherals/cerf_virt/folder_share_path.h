#pragma once

#define NOMINMAX
#include <windows.h>

#include "../../core/service.h"

#include <cstdint>
#include <string>

class FolderSharePath : public Service {
public:
    using Service::Service;

    uint16_t ToWin32Path(const uint16_t* ce_name, uint16_t ce_len_bytes,
                         std::wstring& out) const;

    static uint32_t FiletimeToLong(const FILETIME& ft);
    static bool     LongToFiletime(uint32_t dos_datetime, FILETIME& out);
    static uint16_t CeFileAttributes(DWORD win32_attrs);
    static uint32_t CeFileSize(DWORD size_high, DWORD size_low);
    static uint16_t CeError(DWORD win32_error);
};
