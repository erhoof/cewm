#pragma once

#include <cstdint>

/* Standard PC keyboard geometry (key units) + legends for the replica dialog.
   Modifier caps key to the generic VKs (Shift 0x10 / Ctrl 0x11 / Alt 0x12): a
   board folds L/R onto those, so an L/R-specific VK here would dim a cap whose
   key the board actually maps. */
struct KeyCap {
    uint8_t        vk;
    const wchar_t* legend;
    float          x;
    float          y;
    float          w;
};

inline constexpr KeyCap kPcKeyboard[] = {
    /* Function row. */
    { 0x1B, L"Esc",  0.0f, 0.0f, 1.0f },
    { 0x70, L"F1",   2.0f, 0.0f, 1.0f }, { 0x71, L"F2",  3.0f, 0.0f, 1.0f },
    { 0x72, L"F3",   4.0f, 0.0f, 1.0f }, { 0x73, L"F4",  5.0f, 0.0f, 1.0f },
    { 0x74, L"F5",   6.5f, 0.0f, 1.0f }, { 0x75, L"F6",  7.5f, 0.0f, 1.0f },
    { 0x76, L"F7",   8.5f, 0.0f, 1.0f }, { 0x77, L"F8",  9.5f, 0.0f, 1.0f },
    { 0x78, L"F9",  11.0f, 0.0f, 1.0f }, { 0x79, L"F10", 12.0f, 0.0f, 1.0f },
    { 0x7A, L"F11", 13.0f, 0.0f, 1.0f }, { 0x7B, L"F12", 14.0f, 0.0f, 1.0f },

    /* Number row. */
    { 0xC0, L"`",  0.0f, 1.25f, 1.0f },
    { 0x31, L"1",  1.0f, 1.25f, 1.0f }, { 0x32, L"2", 2.0f, 1.25f, 1.0f },
    { 0x33, L"3",  3.0f, 1.25f, 1.0f }, { 0x34, L"4", 4.0f, 1.25f, 1.0f },
    { 0x35, L"5",  5.0f, 1.25f, 1.0f }, { 0x36, L"6", 6.0f, 1.25f, 1.0f },
    { 0x37, L"7",  7.0f, 1.25f, 1.0f }, { 0x38, L"8", 8.0f, 1.25f, 1.0f },
    { 0x39, L"9",  9.0f, 1.25f, 1.0f }, { 0x30, L"0", 10.0f, 1.25f, 1.0f },
    { 0xBD, L"-", 11.0f, 1.25f, 1.0f }, { 0xBB, L"=", 12.0f, 1.25f, 1.0f },
    { 0x08, L"Bksp", 13.0f, 1.25f, 2.0f },

    /* QWERTY row. */
    { 0x09, L"Tab", 0.0f, 2.25f, 1.5f },
    { 0x51, L"Q",  1.5f, 2.25f, 1.0f }, { 0x57, L"W", 2.5f, 2.25f, 1.0f },
    { 0x45, L"E",  3.5f, 2.25f, 1.0f }, { 0x52, L"R", 4.5f, 2.25f, 1.0f },
    { 0x54, L"T",  5.5f, 2.25f, 1.0f }, { 0x59, L"Y", 6.5f, 2.25f, 1.0f },
    { 0x55, L"U",  7.5f, 2.25f, 1.0f }, { 0x49, L"I", 8.5f, 2.25f, 1.0f },
    { 0x4F, L"O",  9.5f, 2.25f, 1.0f }, { 0x50, L"P", 10.5f, 2.25f, 1.0f },
    { 0xDB, L"[", 11.5f, 2.25f, 1.0f }, { 0xDD, L"]", 12.5f, 2.25f, 1.0f },
    { 0xDC, L"\\", 13.5f, 2.25f, 1.5f },

    /* Home row. */
    { 0x14, L"Caps", 0.0f, 3.25f, 1.75f },
    { 0x41, L"A",  1.75f, 3.25f, 1.0f }, { 0x53, L"S", 2.75f, 3.25f, 1.0f },
    { 0x44, L"D",  3.75f, 3.25f, 1.0f }, { 0x46, L"F", 4.75f, 3.25f, 1.0f },
    { 0x47, L"G",  5.75f, 3.25f, 1.0f }, { 0x48, L"H", 6.75f, 3.25f, 1.0f },
    { 0x4A, L"J",  7.75f, 3.25f, 1.0f }, { 0x4B, L"K", 8.75f, 3.25f, 1.0f },
    { 0x4C, L"L",  9.75f, 3.25f, 1.0f }, { 0xBA, L";", 10.75f, 3.25f, 1.0f },
    { 0xDE, L"'", 11.75f, 3.25f, 1.0f },
    { 0x0D, L"Enter", 12.75f, 3.25f, 2.25f },

    /* Shift row. */
    { 0x10, L"Shift", 0.0f, 4.25f, 2.25f },
    { 0x5A, L"Z",  2.25f, 4.25f, 1.0f }, { 0x58, L"X", 3.25f, 4.25f, 1.0f },
    { 0x43, L"C",  4.25f, 4.25f, 1.0f }, { 0x56, L"V", 5.25f, 4.25f, 1.0f },
    { 0x42, L"B",  6.25f, 4.25f, 1.0f }, { 0x4E, L"N", 7.25f, 4.25f, 1.0f },
    { 0x4D, L"M",  8.25f, 4.25f, 1.0f }, { 0xBC, L",", 9.25f, 4.25f, 1.0f },
    { 0xBE, L".", 10.25f, 4.25f, 1.0f }, { 0xBF, L"/", 11.25f, 4.25f, 1.0f },
    { 0x10, L"Shift", 12.25f, 4.25f, 2.75f },

    /* Bottom row. */
    { 0x11, L"Ctrl", 0.0f, 5.25f, 1.25f },
    { 0x5B, L"Win",  1.25f, 5.25f, 1.25f },
    { 0x12, L"Alt",  2.5f, 5.25f, 1.25f },
    { 0x20, L"Space", 3.75f, 5.25f, 6.25f },
    { 0x12, L"Alt",  10.0f, 5.25f, 1.25f },
    { 0x5C, L"Win", 11.25f, 5.25f, 1.25f },
    { 0x5D, L"Menu", 12.5f, 5.25f, 1.25f },
    { 0x11, L"Ctrl", 13.75f, 5.25f, 1.25f },

    /* Editing + navigation cluster. */
    { 0x2D, L"Ins",  15.5f, 1.25f, 1.0f }, { 0x24, L"Home", 16.5f, 1.25f, 1.0f },
    { 0x21, L"PgUp", 17.5f, 1.25f, 1.0f },
    { 0x2E, L"Del",  15.5f, 2.25f, 1.0f }, { 0x23, L"End",  16.5f, 2.25f, 1.0f },
    { 0x22, L"PgDn", 17.5f, 2.25f, 1.0f },

    /* Arrows. */
    { 0x26, L"↑", 16.5f, 4.25f, 1.0f },
    { 0x25, L"←", 15.5f, 5.25f, 1.0f },
    { 0x28, L"↓", 16.5f, 5.25f, 1.0f },
    { 0x27, L"→", 17.5f, 5.25f, 1.0f },
};

inline constexpr float kPcKeyboardUnitsW = 18.5f;
inline constexpr float kPcKeyboardUnitsH = 6.25f;
