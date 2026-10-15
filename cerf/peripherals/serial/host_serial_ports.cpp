#include "host_serial_ports.h"

#include "../../core/cerf_emulator.h"

#define NOMINMAX
#include <windows.h>

REGISTER_SERVICE(HostSerialPorts);

std::vector<std::wstring> HostSerialPorts::Enumerate() const {
    std::vector<std::wstring> ports;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return ports;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        wchar_t data[256];
        DWORD nlen = 256, dlen = sizeof(data), type = 0;
        const LONG rc = RegEnumValueW(key, i, name, &nlen, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(data), &dlen);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS && type == REG_SZ) {
            data[(sizeof(data) / sizeof(wchar_t)) - 1] = L'\0';
            ports.emplace_back(data);
        }
    }
    RegCloseKey(key);
    return ports;
}
