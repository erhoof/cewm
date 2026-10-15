#pragma once

#include "../core/service.h"

#include <cstdint>

/* The bottom CPU-activity bar: a scrolling strip that advances off the host
   animation clock, so it freezes when emulation is paused (a visible
   "alive / paused" indicator). Shared by the Boot Screen and Hardware Screen
   tabs. */
class BootBar : public Service {
public:
    using Service::Service;

    /* Blend the scrolling bar into the bottom of the BGRA32 surface. */
    void RenderInto(uint32_t* dib_bgra32, uint32_t width, uint32_t height);
};
