#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>

struct KeyCap;
struct KeyBinding;

/* Modal dialog showing the host PC keyboard as a replica: each key is dimmed
   when the board maps nothing to it, else lit with its legend and (when the
   guest action differs) what it maps to. Clicking a holdable modifier (e.g. Fn)
   previews that layer's bindings. UI-thread only. */
class KeyboardMappingDialog : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Show();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void Paint(HDC dc);
    void DrawCap(HDC dc, const KeyCap& cap);
    RECT CapRect(const KeyCap& cap) const;
    RECT ToggleRect(int index) const;   /* lock-layer checkbox in the header. */
    const KeyBinding* FindBinding(uint8_t vk, uint8_t layer) const;
    void OnClick(int x, int y);

    HWND    hwnd_         = nullptr;
    uint8_t active_layer_ = 0;   /* 0 = base; >0 = a modifier/lock-layer preview. */
    HFONT   cap_font_     = nullptr;   /* slightly smaller than the UI font. */
};
