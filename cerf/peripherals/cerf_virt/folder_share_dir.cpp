#include "folder_share_dir.h"

#include "folder_share_path.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/folder_share_config.h"
#include "../../core/log.h"
#include "../../socs/guest_cpu_reset.h"

#include <cstring>
#include <cwchar>
#include <string>

using namespace CerfVirt;

REGISTER_SERVICE(FolderShareDir);

namespace {

bool IsDotName(const wchar_t* n) {
    return n[0] == L'.' && (n[1] == L'\0' || (n[1] == L'.' && n[2] == L'\0'));
}

void FillFromFindData(ServerPB& pb, const WIN32_FIND_DATAW& fd) {
    pb.fFileAttributes     = FolderSharePath::CeFileAttributes(fd.dwFileAttributes);
    pb.fSize               = FolderSharePath::CeFileSize(fd.nFileSizeHigh, fd.nFileSizeLow);
    pb.fFileTimeDate       = FolderSharePath::FiletimeToLong(fd.ftLastWriteTime);
    pb.fFileCreateTimeDate = FolderSharePath::FiletimeToLong(fd.ftCreationTime);
    size_t n = wcslen(fd.cFileName);
    if (n > kMaxLfn) n = kMaxLfn;
    memcpy(pb.fLfn.fName, fd.cFileName, n * sizeof(uint16_t));
    pb.fLfn.fName[n]   = 0;
    pb.fLfn.fNameLength = (uint16_t)(n * sizeof(uint16_t));
}

}

FolderShareDir::~FolderShareDir() {
    CloseAll();
}

bool FolderShareDir::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void FolderShareDir::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { CloseAll(); });
}

void FolderShareDir::CloseAll() {
    for (auto& entry : finds_) FindClose(entry.second);
    finds_.clear();
}

void FolderShareDir::ReconcileGeneration() {
    const uint32_t g = emu_.Get<FolderShareConfig>().Generation();
    if (g != config_generation_) {
        config_generation_ = g;
        CloseAll();
    }
}

bool FolderShareDir::Owns(uint32_t code) {
    return code == kServerMkDir || code == kServerRmDir ||
           code == kServerSetAttributes || code == kServerRename ||
           code == kServerDelete || code == kServerGetInfo ||
           code == kServerFindClose;
}

uint32_t FolderShareDir::Run(uint32_t code, ServerPB& pb) {
    switch (code) {
        case kServerMkDir:         return MkDir(pb);
        case kServerRmDir:         return RmDir(pb);
        case kServerSetAttributes: return SetAttributes(pb);
        case kServerRename:        return Rename(pb);
        case kServerDelete:        return Delete(pb);
        case kServerGetInfo:       return GetInfo(pb);
        case kServerFindClose:     return CloseFind(pb);
        default: break;
    }
    LOG(Cerf, "[FolderShareDir] Run called with unowned op 0x%X\n", code);
    CerfFatalExit();
}

uint16_t FolderShareDir::MkDir(ServerPB& pb) {
    std::wstring path;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    if (!CreateDirectoryW(path.c_str(), nullptr))
        return FolderSharePath::CeError(GetLastError());
    return kErrorNoError;
}

uint16_t FolderShareDir::RmDir(ServerPB& pb) {
    std::wstring path;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    if (!RemoveDirectoryW(path.c_str()))
        return FolderSharePath::CeError(GetLastError());
    return kErrorNoError;
}

uint16_t FolderShareDir::Delete(ServerPB& pb) {
    std::wstring path;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;
    if (!DeleteFileW(path.c_str()))
        return FolderSharePath::CeError(GetLastError());
    return kErrorNoError;
}

uint16_t FolderShareDir::Rename(ServerPB& pb) {
    FolderSharePath& fsp = emu_.Get<FolderSharePath>();
    std::wstring src, dst;
    uint16_t e = fsp.ToWin32Path(pb.fLfn.fName, pb.fLfn.fNameLength, src);
    if (e != kErrorNoError) return e;
    e = fsp.ToWin32Path(pb.fLfn.fName2, pb.fLfn.fName2Length, dst);
    if (e != kErrorNoError) return e;
    if (!MoveFileW(src.c_str(), dst.c_str()))
        return FolderSharePath::CeError(GetLastError());
    return kErrorNoError;
}

uint16_t FolderShareDir::SetAttributes(ServerPB& pb) {
    std::wstring path;
    uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
        pb.fLfn.fName, pb.fLfn.fNameLength, path);
    if (e != kErrorNoError) return e;

    if (pb.fFileTimeDate != 0) {
        FILETIME write_ft;
        if (!FolderSharePath::LongToFiletime(pb.fFileTimeDate, write_ft))
            return kErrorGeneralFailure;
        HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return FolderSharePath::CeError(GetLastError());
        const BOOL ok = SetFileTime(h, nullptr, nullptr, &write_ft);
        const DWORD err = ok ? 0 : GetLastError();
        CloseHandle(h);
        if (!ok) return FolderSharePath::CeError(err);
    }

    DWORD attrs = pb.fFileAttributes &
                  (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                   FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
    if (attrs == 0) attrs = FILE_ATTRIBUTE_NORMAL;
    if (!SetFileAttributesW(path.c_str(), attrs))
        return FolderSharePath::CeError(GetLastError());
    return kErrorNoError;
}

uint16_t FolderShareDir::CloseFind(ServerPB& pb) {
    auto it = finds_.find(pb.fFindTransactionID);
    if (it != finds_.end()) {
        FindClose(it->second);
        finds_.erase(it);
    }
    return kErrorNoError;
}

uint16_t FolderShareDir::GetInfo(ServerPB& pb) {
    if (pb.fIndex == -1) {
        std::wstring path;
        uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
            pb.fLfn.fName, pb.fLfn.fNameLength, path);
        if (e != kErrorNoError) return e;
        WIN32_FILE_ATTRIBUTE_DATA ad;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &ad))
            return FolderSharePath::CeError(GetLastError());
        pb.fFileAttributes     = FolderSharePath::CeFileAttributes(ad.dwFileAttributes);
        pb.fSize               = FolderSharePath::CeFileSize(ad.nFileSizeHigh, ad.nFileSizeLow);
        pb.fFileTimeDate       = FolderSharePath::FiletimeToLong(ad.ftLastWriteTime);
        pb.fFileCreateTimeDate = FolderSharePath::FiletimeToLong(ad.ftCreationTime);
        return kErrorNoError;
    }

    const uint32_t tid = pb.fFindTransactionID;
    WIN32_FIND_DATAW fd;
    if (pb.fIndex == 0) {
        auto stale = finds_.find(tid);
        if (stale != finds_.end()) {
            FindClose(stale->second);
            finds_.erase(stale);
        }
        std::wstring spec;
        uint16_t e = emu_.Get<FolderSharePath>().ToWin32Path(
            pb.fLfn.fName, pb.fLfn.fNameLength, spec);
        if (e != kErrorNoError) return e;
        HANDLE h = FindFirstFileW(spec.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            return (err == ERROR_FILE_NOT_FOUND)
                       ? kErrorNoMoreFiles
                       : FolderSharePath::CeError(err);
        }
        while (IsDotName(fd.cFileName)) {
            if (!FindNextFileW(h, &fd)) {
                FindClose(h);
                return kErrorNoMoreFiles;
            }
        }
        finds_[tid] = h;
        FillFromFindData(pb, fd);
        return kErrorNoError;
    }

    auto it = finds_.find(tid);
    if (it == finds_.end()) return kErrorNoMoreFiles;
    do {
        if (!FindNextFileW(it->second, &fd)) {
            FindClose(it->second);
            finds_.erase(it);
            return kErrorNoMoreFiles;
        }
    } while (IsDotName(fd.cFileName));
    FillFromFindData(pb, fd);
    return kErrorNoError;
}
