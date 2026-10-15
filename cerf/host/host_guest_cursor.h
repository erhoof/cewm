#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

/* Turns the guest's captured cursor shape (CerfVirtCursor) into a host HCURSOR,
   rebuilt on the UI thread when the shape changes so all GDI cursor calls stay
   on one thread. */
class HostGuestCursor : public Service {
public:
    using Service::Service;
    ~HostGuestCursor() override;

    /* UI thread (WM_SETCURSOR). active=false => no guest cursor exists (caller
       shows the host's own cursor). active=true => return value is the guest's
       HCURSOR, or NULL when the guest hid its cursor. */
    HCURSOR Resolve(bool& active);

private:
    HCURSOR  built_    = nullptr;
    uint32_t last_seq_ = 0;
    bool     active_   = false;
};
