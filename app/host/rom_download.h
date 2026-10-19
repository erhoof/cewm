// Downloads a ROM bundle from the cerf-bundles repository and unpacks it (see rom_download.cpp).
#pragma once
#include <string>

namespace cewm::romdl {
struct State {
    int phase = 0;  // 0 idle, 1 busy, 2 done, 3 failed
    int pct = 0;
    std::string text;  // what is going on / the error
    std::string rom;   // the primary ROM file, once phase == 2
    std::string board, label;  // from the manifest: the CERF board id and the system name
};
// Fetches bundle `name` into dir/<name>/ on a background thread; ignored while one is running.
void Start(const std::string& name, const std::string& dir);
State Get();
// Unpacks the files of a local .zip whose names end in only_ext (all when null) into dir.
bool UnzipFile(const std::string& zip, const std::string& dir, const char* only_ext, std::string& err);
void Reset();  // back to idle after the result was taken
}  // namespace cewm::romdl
