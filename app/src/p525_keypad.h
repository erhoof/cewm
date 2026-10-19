#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>
#include <SDL_opengles2.h>
#include <imgui.h>
#include "emu_runner.h"
#include "../host/haptic.h"
#include "p525_icons.h"

// P525 front keys, with the joystick expanded into a separate navigation row.
// Send Windows Mobile virtual keys; the guest's IME owns T9 and multi-tap text entry.
// Multi-touch: every pointer (the mouse, or each finger) latches the key it went down on and holds it until it lifts, so several keys
// can be down at once (chords in games). Pointers are keyed by id; fingers come from main's SDL_FINGER* handling in UI coordinates.
inline std::vector<std::pair<long long, ImVec2>> g_p525_fingers;
inline std::vector<std::pair<long long, ImGuiID>> g_p525_latch;  // pointer id -> key id it holds (0: went down off the keys)
inline std::vector<ImGuiID> g_p525_down;                          // key ids held right now (for drawing)
inline bool g_p525_vk_held[256] = {};

inline void ReleaseP525Key() {
    for (int vk = 0; vk < 256; ++vk) if (g_p525_vk_held[vk]) { cewm::EmuKey((uint8_t)vk, true); g_p525_vk_held[vk] = false; }
    g_p525_latch.clear();
    g_p525_down.clear();
}

inline void DrawP525Keypad(float x, float y, float width, float height, bool bg = false) {
    struct KeyRect { ImGuiID id; uint8_t vk; ImVec2 a, b; };
    std::vector<KeyRect> rects;
    const float gap = width * (bg ? 0.025f : 0.012f);
    const float cw = width / 5.f, rh = height / 6.7f;  // joystick pad 2.7 rows + 4 key rows
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID("p525-keypad");
    int next_id = 0;
    auto key = [&](const char* label, const char* letters, uint8_t vk,
                   float col, float row, float rows = 1.f, ImU32 color = IM_COL32(235, 238, 242, 255), float cols = 1.f) {
        ImGui::PushID(next_id++);  // row/col alone repeat once y moves down, so the arrows shared IDs with the keys below
        ImGui::SetCursorPos({x + cw * col + gap / 2, y + rh * row + gap / 2});
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 size = {cw * cols - gap, rh * rows - gap};
        ImGui::InvisibleButton("key", size);
        const ImGuiID id = ImGui::GetItemID();
        rects.push_back({id, vk, p, {p.x + size.x, p.y + size.y}});
        const bool active = std::find(g_p525_down.begin(), g_p525_down.end(), id) != g_p525_down.end();
        if (bg) dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, active ? IM_COL32(85, 88, 94, 255) : IM_COL32(48, 50, 54, 255), std::min(size.x, size.y) * 0.12f);
        color = (color & 0x00FFFFFFu) | ((ImU32)(active ? 255 : 200) << 24);
        if (label[0] == 'o') {  // centre of the joystick: a plain circle
            const ImVec2 c = {p.x + size.x / 2, p.y + size.y / 2};
            dl->AddCircle(c, std::min(size.x, size.y) * 0.30f, color, 0, 6.f);
            ImGui::PopID();
            return;
        }
        if (vk >= 0x25 && vk <= 0x28) {
            const float pull_x = 0.f;
            const ImVec2 c = {p.x + size.x * (0.5f + pull_x), p.y + size.y / 2};
            const float r = std::min(size.x, size.y) * 0.30f;
            const ImVec2 d = {vk == 0x25 ? -1.f : vk == 0x27 ? 1.f : 0.f,
                              vk == 0x26 ? -1.f : vk == 0x28 ? 1.f : 0.f};
            const ImVec2 tip = {c.x + d.x * r, c.y + d.y * r};
            dl->AddLine({c.x - d.x * r, c.y - d.y * r}, tip, color, 6.f);
            dl->AddLine(tip, {c.x - d.y * r * 0.7f, c.y + d.x * r * 0.7f}, color, 5.f);
            dl->AddLine(tip, {c.x + d.y * r * 0.7f, c.y - d.x * r * 0.7f}, color, 5.f);
            ImGui::PopID();
            return;
        }
        if (vk == 0x72 || vk == 0x73) {  // Call / End: glyph masks, tinted
            static GLuint tex[2];
            const int which = vk == 0x73;
            if (!tex[which]) {
                const unsigned char* m = which ? kIco_end : kIco_call;
                std::vector<unsigned char> rgba(96 * 96 * 4, 255);
                for (int i = 0; i < 96 * 96; i++) rgba[i * 4 + 3] = m[i];
                glGenTextures(1, &tex[which]);
                glBindTexture(GL_TEXTURE_2D, tex[which]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 96, 96, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            }
            const ImVec2 c = {p.x + size.x / 2, p.y + size.y / 2};
            const float h = std::min(size.x, size.y) * 0.32f;
            dl->AddImage((ImTextureID)(intptr_t)tex[which], {c.x - h, c.y - h}, {c.x + h, c.y + h}, {0, 0}, {1, 1}, color);
            ImGui::PopID();
            return;
        }
        if (vk == 0x5B) {  // Start: Windows flag, white
            const ImVec2 c = {p.x + size.x / 2, p.y + size.y / 2};
            const float r = std::min(size.x, size.y) * 0.30f, g = r * 0.12f;
            auto Q = [&](float x0, float y0, float x1, float y1) {  // one pane, tilted like the flag waves
                const float w = 0.12f * r;
                dl->AddQuadFilled({c.x + x0 * r, c.y + y0 * r + w}, {c.x + x1 * r, c.y + y0 * r - w},
                                  {c.x + x1 * r, c.y + y1 * r - w}, {c.x + x0 * r, c.y + y1 * r + w}, color);
            };
            (void)g;
            Q(-1.f, -0.95f, -0.06f, -0.06f); Q(0.06f, -0.95f, 1.f, -0.06f);
            Q(-1.f, 0.06f, -0.06f, 0.95f);   Q(0.06f, 0.06f, 1.f, 0.95f);
            ImGui::PopID();
            return;
        }
        if (vk == 0x5C) {  // OK: small label in a circle
            const ImVec2 c = {p.x + size.x / 2, p.y + size.y / 2};
            const float r = std::min(size.x, size.y) * 0.30f, f = r * 0.9f;
            dl->AddCircle(c, r, color, 0, 2.5f);
            const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(f, FLT_MAX, 0.f, "ok");
            dl->AddText(ImGui::GetFont(), f, {c.x - t.x / 2, c.y - t.y / 2}, color, "ok");
            ImGui::PopID();
            return;
        }
        // Letters come as "latin\ncyrillic" lines: digit on the left, the two letter rows stacked on the right.
        const int lines = letters ? 1 + (int)std::count(letters, letters + strlen(letters), '\n') : 0;
        if (lines >= 1) {
            const float fs = std::min(cw * 0.40f, size.y * 0.60f);
            const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, label);
            const float bold = std::max(0.6f, fs * 0.03f);
            for (float dx : {0.f, bold})
                dl->AddText(ImGui::GetFont(), fs, {p.x + size.x * 0.13f - 1.f + dx, p.y + (size.y - text.y) / 2}, color, label);
            const float x0 = p.x + size.x * 0.40f, avail = size.x * 0.56f;
            float small = size.y * 0.34f;
            for (int i = 0; i < lines; ++i) {  // shrink until the widest row fits
                const char* b0 = letters; for (int k = 0; k < i; ++k) b0 = strchr(b0, '\n') + 1;
                const char* e0 = strchr(b0, '\n');
                small = std::min(small, small * avail / std::max(avail, ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.f, b0, e0).x));
            }
            const float lh = small * 1.15f, y0 = p.y + (size.y - lines * lh) / 2;
            for (int i = 0; i < lines; ++i) {
                const char* b0 = letters; for (int k = 0; k < i; ++k) b0 = strchr(b0, '\n') + 1;
                const char* e0 = strchr(b0, '\n');
                for (float dx : {0.f, std::max(0.4f, small * 0.03f)})
                    dl->AddText(ImGui::GetFont(), small, {x0 + dx, y0 + i * lh}, IM_COL32(200, 210, 222, 255), b0, e0);
            }
            ImGui::PopID();
            return;
        }
        const float fs = std::min(cw * 0.30f, size.y * (letters ? 0.43f : 0.55f));
        const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, label);
        const float small = fs * 0.7f, lh = small * 1.12f;
        const float total = text.y + lines * lh + (lines == 1 ? fs * 0.14f : 0.f);
        const float top = p.y + (size.y - total) / 2;
        const float bold = std::max(0.6f, fs * 0.03f);  // fake bold: the same text again, shifted a hair
        for (float dx : {0.f, bold}) dl->AddText(ImGui::GetFont(), fs, {p.x + (size.x - text.x) / 2 + dx, top}, color, label);
        for (int i = 0; i < lines; ++i) {
            const char* b0 = letters;
            const char* e0 = strchr(b0, '\n');
            const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.f, b0, e0);
            for (float dx : {0.f, std::max(0.4f, small * 0.03f)})
                dl->AddText(ImGui::GetFont(), small, {p.x + (size.x - ts.x) / 2 + dx, top + text.y + i * lh}, IM_COL32(200, 210, 222, 255), b0, e0);
        }
        ImGui::PopID();
    };

    // Bare keys on the app's black background, like the existing soft-button strip.

    // Navigation block, 3 rows x 5 columns:
    //   soft1   up    soft2
    //   call  < o >    end
    //   start mode down ok C
    const float nh = 0.9f;
    key("...", nullptr, 0x70, 0, 0, nh, IM_COL32(235, 238, 242, 255), 2.f);  // VK_TSOFT1
    key("^", nullptr, 0x26, 2, 0, nh);
    key("...", nullptr, 0x71, 3, 0, nh, IM_COL32(235, 238, 242, 255), 2.f);  // VK_TSOFT2
    key("Call", nullptr, 0x72, 0, nh, nh, IM_COL32(65, 210, 95, 255));
    key("<", nullptr, 0x25, 1, nh, nh);
    key("o", nullptr, 0x0D, 2, nh, nh);
    key(">", nullptr, 0x27, 3, nh, nh);
    key("End", nullptr, 0x73, 4, nh, nh, IM_COL32(240, 85, 75, 255));
    key("Start", nullptr, 0x5B, 0, 2 * nh, nh);
    key("Mode", nullptr, 0xC1, 1, 2 * nh, nh);  // task-switcher matrix contact
    key("v", nullptr, 0x28, 2, 2 * nh, nh);
    key("OK", nullptr, 0x5C, 3, 2 * nh, nh);    // OEM OK/Close contact, distinct from joystick Enter
    key("C", nullptr, 0x08, 4, 2 * nh, nh);     // Back contact (OEM scan 13)
    y += rh * 2.7f;

    // T9 pad in three columns.
    const float t = 5.f / 3;
    auto k9 = [&](const char* l, const char* s, uint8_t vk, int col, int row) { key(l, s, vk, col * t, row, 1.f, IM_COL32(235, 238, 242, 255), t); };
    k9("1", "@", 0x31, 0, 0);
    k9("2", "abc\nабвг", 0x32, 1, 0);
    k9("3", "def\nдежз", 0x33, 2, 0);
    k9("4", "ghi\nийкл", 0x34, 0, 1);
    k9("5", "jkl\nмноп", 0x35, 1, 1);
    k9("6", "mno\nрсту", 0x36, 2, 1);
    k9("7", "pqrs\nфхцч", 0x37, 0, 2);
    k9("8", "tuv\nшщъы", 0x38, 1, 2);
    k9("9", "wxyz\nьэюя", 0x39, 2, 2);
    k9("*", nullptr, 0x77, 0, 3); // VK_TSTAR
    k9("0", "+", 0x30, 1, 3);
    k9("#", nullptr, 0x78, 2, 3); // VK_TPOUND


    // Resolve the pointers after the layout: new ones latch the key under them, lifted ones let go; the keys held are the union.
    {
        std::vector<std::pair<long long, ImVec2>> ptrs = g_p525_fingers;
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDown[0]) ptrs.push_back({-1, io.MousePos});
        g_p525_latch.erase(std::remove_if(g_p525_latch.begin(), g_p525_latch.end(), [&](auto& l) {
            return std::none_of(ptrs.begin(), ptrs.end(), [&](auto& q) { return q.first == l.first; }); }), g_p525_latch.end());
        for (auto& q : ptrs) {
            if (std::any_of(g_p525_latch.begin(), g_p525_latch.end(), [&](auto& l) { return l.first == q.first; })) continue;
            ImGuiID hit = 0;
            for (auto& r : rects) if (q.second.x >= r.a.x && q.second.x < r.b.x && q.second.y >= r.a.y && q.second.y < r.b.y) hit = r.id;
            g_p525_latch.push_back({q.first, hit});
        }
        g_p525_down.clear();
        bool want[256] = {};
        for (auto& l : g_p525_latch) {
            if (!l.second) continue;
            g_p525_down.push_back(l.second);
            for (auto& r : rects) if (r.id == l.second) want[r.vk] = true;
        }
        for (int vk = 0; vk < 256; ++vk) {
            if (want[vk] == g_p525_vk_held[vk]) continue;
            cewm::EmuKey((uint8_t)vk, !want[vk]);
            if (want[vk]) cewm::HapticTick();
            g_p525_vk_held[vk] = want[vk];
        }
    }
    ImGui::PopID();
}
