#include "cerf_demo.h"

#ifndef ENUM_CURRENT_SETTINGS
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#endif

extern BOOL WINAPI EnumDisplaySettings(LPCTSTR, DWORD, LPDEVMODE);

int GuestRefreshHz(void) {
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm) &&
        (dm.dmFields & DM_DISPLAYFREQUENCY))
        return (int)dm.dmDisplayFrequency;
    return 0;
}

static LPCTSTR CpuArchName(WORD arch) {
    switch (arch) {
    case PROCESSOR_ARCHITECTURE_ARM:   return TEXT("ARM");
    case PROCESSOR_ARCHITECTURE_MIPS:  return TEXT("MIPS");
    case PROCESSOR_ARCHITECTURE_SHX:   return TEXT("SHx");
    case PROCESSOR_ARCHITECTURE_INTEL: return TEXT("x86");
    case PROCESSOR_ARCHITECTURE_PPC:   return TEXT("PowerPC");
    case PROCESSOR_ARCHITECTURE_ALPHA: return TEXT("Alpha");
    }
    return TEXT("unknown");
}

static void FormatUptime(TCHAR* out, DWORD ms) {
    DWORD secs = ms / 1000;
    DWORD d = secs / 86400;
    DWORD h = (secs % 86400) / 3600;
    DWORD m = (secs % 3600) / 60;
    DWORD s = secs % 60;
    if (d)
        wsprintf(out, TEXT("%ud %02u:%02u:%02u"), d, h, m, s);
    else
        wsprintf(out, TEXT("%02u:%02u:%02u"), h, m, s);
}

void BuildStats(TCHAR* buf) {
    OSVERSIONINFO osv;
    MEMORYSTATUS  ms;
    SYSTEM_INFO   si;
    TCHAR         uptime[32];
    int           hz = GuestRefreshHz();

    osv.dwOSVersionInfoSize = sizeof(osv); GetVersionEx(&osv);
    ms.dwLength = sizeof(ms); GlobalMemoryStatus(&ms);
    GetSystemInfo(&si);
    FormatUptime(uptime, GetTickCount());

    wsprintf(buf,
        TEXT("CE Runtime Foundation - guest diagnostics\r\n\r\n")
        TEXT("OS: Windows CE %u.%u (build %u)\r\n")
        TEXT("CPU: %s level %u rev %u (type %u) x%u\r\n")
        TEXT("Screen: %d x %d px (%d Hz)\r\n")
        TEXT("Memory: %u KB total / %u KB free\r\n")
        TEXT("Page size: %u bytes\r\n")
        TEXT("Processes: %d / Threads: %d\r\n")
        TEXT("Uptime: %s"),
        osv.dwMajorVersion, osv.dwMinorVersion, osv.dwBuildNumber,
        CpuArchName(si.wProcessorArchitecture), si.wProcessorLevel,
        si.wProcessorRevision, si.dwProcessorType, si.dwNumberOfProcessors,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), hz,
        (unsigned)(ms.dwTotalPhys / 1024), (unsigned)(ms.dwAvailPhys / 1024),
        si.dwPageSize,
        CountProcesses(), CountThreads(),
        uptime);
}
