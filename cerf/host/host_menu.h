#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

/* The host window's menu bar: builds it, keeps its check/radio state in
   sync, and dispatches its WM_COMMAND ids. UI thread only. */
class HostMenu : public Service {
public:
    using Service::Service;

    /* Build the bar. Called once by HostWindow before CreateWindowExW;
       the bar's lifetime is tied to the window that adopts it. */
    HMENU Build();

    /* WM_INITMENUPOPUP: sync check state; rebuild the Actions popup. */
    void OnInitMenuPopup(HMENU popup);

    /* Menu WM_COMMAND ids not owned by HostWidgetRegistry. */
    void HandleCommand(int id);

private:
    void Sync();

    HMENU bar_ = nullptr;
};
