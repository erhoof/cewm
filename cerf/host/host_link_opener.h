#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

class HostLinkOpener : public Service {
public:
    using Service::Service;

    void Open(HWND owner, const wchar_t* url);

    bool OpenNotified(HWND owner, LPARAM syslink_notify);
};
