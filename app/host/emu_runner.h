// App-facing handle on the CERF emulator: start it, pull frames out.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cewm {
// Boots devemu with `rom` at w x h. Config files come from the dir next to the executable.
// `state_path` is the saved-machine file for this ROM and size; with `resume` an existing file is restored instead of cold-booting.
void EmuStart(const std::string& rom, int w, int h, const std::string& state_path = "", bool resume = false);
// With `save` the machine state is written to the state path after the guest is stopped.
void EmuStop(bool save = false);
void EmuSnapshot();
// If the guest has put up a frame, resizes `bgra` to the guest size and fills it (0xAARRGGBB pixels). Returns false before that.
bool EmuGrabFrame(std::vector<uint32_t>& bgra, uint32_t& w, uint32_t& h);
// Input from the frontend, in guest pixel coordinates / Windows virtual-key codes.
// (x, y) in guest pixels of a w x h guest screen. With Guest Additions the absolute GA pointer is used (resolution independent), else the stock touch panel.
void EmuPen(bool down, int x, int y, int w, int h);
void EmuKey(uint8_t vk, bool up);
bool EmuRunning();
// Guest Additions only: asks the guest driver to switch to w x h while running (no reset). False if the guest has no GA.
bool EmuResize(int w, int h);
}  // namespace cewm
