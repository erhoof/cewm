#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <map>
#include <string>
#include <utility>

/* Renders cerf.rc ICON resources for status-bar widgets, sized to fit the
   widget box, with a per-(resource,size) HICON cache. UI-thread only. */
class HostIconCache : public Service {
public:
    using Service::Service;
    ~HostIconCache() override;

    void DrawCentered(HDC dc, const RECT& box, const wchar_t* res_name);

private:
    HICON Resolve(const wchar_t* res_name, int px);

    std::map<std::pair<std::wstring, int>, HICON> cache_;
};
