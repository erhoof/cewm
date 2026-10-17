// CERF builds Windows paths with '\' ("devices\<name>\cerf.json"); the POSIX forks open them through this.
#pragma once
#include <string>
inline std::string CerfPosixPath(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}
