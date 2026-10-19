// Turns an ASUS P525 firmware (the vendor .zip, or its .cap) into the bundle the board boots: p525-bridge.nb0 plus the storage, disk and root images
// (the port of app/tools/prepare_p525.py; nothing of the firmware is stored in the app).
#pragma once
#include <functional>
#include <string>

namespace cewm::p525 {
// True for a path that is a vendor archive or a CAP (by extension).
bool IsFirmware(const std::string& path);
// Writes p525-bridge.nb0, p525-storage.bin, p525-disk.bin and p525-root.img into `out`; `status(pct, text)` reports progress. On failure returns false and sets `err`.
bool Prepare(const std::string& path, const std::string& out, const std::function<void(int, const char*)>& status, std::string& err);
}  // namespace cewm::p525
