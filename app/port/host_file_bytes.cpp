// POSIX fork of cerf/cerf/core/host_file_bytes.cpp (MIT).
#include "core/host_file_bytes.h"

#include <sys/stat.h>
#include "cerf_path.h"

#include <fstream>

std::vector<uint8_t> ReadHostFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = f.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), sz);
    return bytes;
}

bool HostFileNonEmpty(const std::string& path) {
    struct stat st;
    return stat(CerfPosixPath(path).c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}
