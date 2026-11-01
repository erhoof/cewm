#pragma once
#include <atomic>
#include <cstring>
#include <string>

namespace cewm {
// 0 follows the system, 1 English, 2 Russian. Workers also translate download errors.
inline std::atomic<int> language{0};
inline bool system_russian = false;
inline bool RussianLocale(const char* locale) {
    return locale && locale[0] == 'r' && locale[1] == 'u' &&
        (!locale[2] || locale[2] == '-' || locale[2] == '_');
}
inline bool Russian() {
    const int choice = language.load();
    return choice == 2 || (choice == 0 && system_russian);
}
inline const char* L(const char* ru, const char* en) { return Russian() ? ru : en; }
// Generated ROM catalogs keep their original Russian descriptions.
inline std::string RomLabel(const char* label) {
    if (Russian()) return label;
    std::string text = label;
    const char* words[][2] = {
        {"демо", "demo"}, {"альт. управление", "alternative controls"}, {"стиль XP", "XP theme"},
        {"сборка", "build"}, {"испанский", "Spanish"}, {"итальянский", "Italian"},
        {"немецкий", "German"}, {"французский", "French"}, {"японский", "Japanese"},
        {"китайский (трад.)", "Traditional Chinese"}, {"китайский (упр.)", "Simplified Chinese"},
        {"русский", "Russian"}
    };
    for (const auto& word : words) {
        size_t at = 0;
        while ((at = text.find(word[0], at)) != std::string::npos) {
            text.replace(at, strlen(word[0]), word[1]);
            at += strlen(word[1]);
        }
    }
    return text;
}
inline bool DeviceEmulator(const char* board) { return !strncmp(board, "devemu", 6); }
}
