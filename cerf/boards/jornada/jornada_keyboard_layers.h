#pragma once

#include "../../host/keyboard_map.h"

#include <cstdint>
#include <vector>

/* Shared HP Jornada (720 + 820) keyboard preview layers: Fn (host F10), Alt
   (host Alt), and a Num Lock numpad overlay. Labels are display-only. */
namespace jornada_kbd {

inline constexpr uint8_t kVkFn         = 0x79;
inline constexpr uint8_t kVkAlt        = 0x12;
inline constexpr uint8_t kNumLockLayer = 3;

struct LayerKey { uint8_t vk; const wchar_t* label; };

/* Layer 1 - Fn held. {}[] are word_FC10A0 col7 on the 720. */
inline constexpr LayerKey kFnLayer[] = {
    { 0x50, L"{" }, { 0xDC, L"}" }, { 0xBA, L"[" }, { 0xDE, L"]" },
    { 0x10, L"Caps Lock" },   /* Fn + Shift */
    { 0x11, L"Num Lock"  },   /* Fn + Ctrl  */
};

/* Layer 2 - Alt held. */
inline constexpr LayerKey kAltLayer[] = {
    { 0x26, L"PgUp" }, { 0x25, L"Home" }, { 0x28, L"PgDn" }, { 0x27, L"End" },
};

/* Layer 3 - Num Lock numpad overlay. */
inline constexpr LayerKey kNumLayer[] = {
    { 0x37, L"7" }, { 0x38, L"8" }, { 0x39, L"9" }, { 0x30, L"/" },
    { 0x55, L"4" }, { 0x49, L"5" }, { 0x4F, L"6" }, { 0x50, L"*" },
    { 0x4A, L"1" }, { 0x4B, L"2" }, { 0x4C, L"3" }, { 0xBA, L"-" },
    { 0x4D, L"0" }, { 0xBC, L"," }, { 0xBE, L"." }, { 0xBF, L"+" },
};

/* Build the full binding set for a Jornada board: base layer from its scancode
   table (with Fn/Alt marked as modifiers) + all three preview layers. */
inline void BuildBindings(const uint8_t scancodes[256],
                          std::vector<KeyBinding>& out) {
    for (int vk = 0; vk < 256; ++vk) {
        const uint8_t sc = scancodes[vk];
        if (!sc) continue;
        KeyBinding b{ static_cast<uint8_t>(vk), sc, nullptr, 0, 0 };
        if (vk == kVkFn)  { b.guest_label = L"Fn";  b.holds_layer = 1; }
        if (vk == kVkAlt) { b.guest_label = L"Alt"; b.holds_layer = 2; }
        out.push_back(b);
    }
    for (const LayerKey& k : kFnLayer)  out.push_back({ k.vk, 0, k.label, 1, 0 });
    for (const LayerKey& k : kAltLayer) out.push_back({ k.vk, 0, k.label, 2, 0 });
    for (const LayerKey& k : kNumLayer)
        out.push_back({ k.vk, 0, k.label, kNumLockLayer, 0 });
}

inline std::vector<KeyboardToggleLayer> ToggleLayers() {
    return { { kNumLockLayer, L"Num Lock" } };
}

}  /* namespace jornada_kbd */
