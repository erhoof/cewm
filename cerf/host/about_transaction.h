#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

class AboutTransaction : public Service {
public:
    using Service::Service;

    bool Open(HWND owner);
};
