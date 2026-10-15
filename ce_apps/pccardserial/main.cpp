#include <windows.h>

#define PNP_MAX  128
#define PATH_MAX 256

static const wchar_t kActiveKey[]   = L"Drivers\\Active";
static const wchar_t kPcmciaKey[]   = L"Drivers\\PCMCIA";
static const wchar_t kModemDevKey[] = L"Drivers\\PCMCIA\\Modem";
static const wchar_t kSerialDll[]   = L"Serial.dll";
static const wchar_t kUnimodemTsp[] = L"Unimodem.dll";
static const wchar_t kComPrefix[]   = L"COM";
static const wchar_t kFriendly[]    = L"Serial Cable on PC Card";
static const wchar_t kTitle[]       = L"CERF PC Card Serial";

static DWORD StrLen(const wchar_t* s) {
    DWORD n = 0;
    while (s[n]) n++;
    return n;
}

static void StrCopy(wchar_t* dst, DWORD cch, const wchar_t* src) {
    DWORD i = 0;
    if (!cch) return;
    while (src[i] && i + 1 < cch) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void StrAppend(wchar_t* dst, DWORD cch, const wchar_t* src) {
    DWORD n = StrLen(dst);
    if (n < cch) StrCopy(dst + n, cch - n, src);
}

static BOOL StrEqualI(const wchar_t* a, const wchar_t* b) {
    DWORD i = 0;
    for (;;) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca + 32);
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb + 32);
        if (ca != cb) return FALSE;
        if (!ca) return TRUE;
        i++;
    }
}

static BOOL ReadString(HKEY key, const wchar_t* name, wchar_t* buf, DWORD cch) {
    DWORD type = 0;
    DWORD len  = cch * sizeof(wchar_t);
    buf[0] = 0;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE*)buf, &len) != ERROR_SUCCESS)
        return FALSE;
    if (type != REG_SZ) return FALSE;
    buf[cch - 1] = 0;
    return buf[0] != 0;
}

static BOOL WriteString(HKEY key, const wchar_t* name, const wchar_t* value) {
    DWORD bytes = (StrLen(value) + 1) * sizeof(wchar_t);
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value, bytes) == ERROR_SUCCESS;
}

static BOOL WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value))
           == ERROR_SUCCESS;
}

static BOOL FindModemBoundPcCard(wchar_t* pnp, DWORD cch) {
    HKEY  active;
    DWORD index = 0;
    BOOL  found = FALSE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kActiveKey, 0, 0, &active) != ERROR_SUCCESS)
        return FALSE;

    for (;;) {
        wchar_t name[32];
        wchar_t devkey[PATH_MAX];
        DWORD   namelen = sizeof(name) / sizeof(name[0]);
        HKEY    device;

        if (RegEnumKeyExW(active, index++, name, &namelen, NULL, NULL, NULL, NULL)
            != ERROR_SUCCESS)
            break;

        if (RegOpenKeyExW(active, name, 0, 0, &device) != ERROR_SUCCESS)
            continue;

        if (ReadString(device, L"Key", devkey, PATH_MAX) &&
            StrEqualI(devkey, kModemDevKey) &&
            ReadString(device, L"PnpId", pnp, cch)) {
            found = TRUE;
        }
        RegCloseKey(device);
        if (found) break;
    }

    RegCloseKey(active);
    return found;
}

static BOOL RegisterAsSerial(const wchar_t* pnp) {
    wchar_t path[PATH_MAX];
    HKEY    key;
    DWORD   disp = 0;
    BOOL    ok;

    StrCopy(path, PATH_MAX, kPcmciaKey);
    StrAppend(path, PATH_MAX, L"\\");
    StrAppend(path, PATH_MAX, pnp);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, 0, NULL, &key, &disp)
        != ERROR_SUCCESS)
        return FALSE;

    ok  = WriteString(key, L"Dll", kSerialDll);
    ok &= WriteString(key, L"Prefix", kComPrefix);
    ok &= WriteDword (key, L"DeviceArrayIndex", 1);
    ok &= WriteString(key, L"Tsp", kUnimodemTsp);
    ok &= WriteDword (key, L"DeviceType", 0);
    ok &= WriteString(key, L"FriendlyName", kFriendly);

    RegCloseKey(key);
    return ok;
}

extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                              LPWSTR cmd, int show) {
    wchar_t pnp[PNP_MAX];
    wchar_t text[PATH_MAX + PNP_MAX];

    (void)hInstance; (void)hPrev; (void)cmd; (void)show;

    if (!FindModemBoundPcCard(pnp, PNP_MAX)) {
        MessageBoxW(NULL,
                    L"No PC Card is bound as a modem right now.\n\n"
                    L"Insert the serial PC Card first, then run this again.",
                    kTitle, MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    StrCopy(text, sizeof(text) / sizeof(text[0]), L"This PC Card is bound as a modem:\n\n");
    StrAppend(text, sizeof(text) / sizeof(text[0]), pnp);
    StrAppend(text, sizeof(text) / sizeof(text[0]),
              L"\n\nRegister it as a serial cable, so it can be used for a direct "
              L"connection instead of dial-up?");

    if (MessageBoxW(NULL, text, kTitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;

    if (!RegisterAsSerial(pnp)) {
        MessageBoxW(NULL, L"Could not write the registry key.",
                    kTitle, MB_OK | MB_ICONEXCLAMATION);
        return 1;
    }

    MessageBoxW(NULL,
                L"Registered.\n\nEject and re-insert the card to apply.",
                kTitle, MB_OK | MB_ICONINFORMATION);
    return 0;
}
