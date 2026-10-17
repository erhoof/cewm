// POSIX fork of cerf_virt/folder_share_{path,dir,files}.cpp (Win32 file APIs -> open/pread/readdir).
// Host paths are UTF-8 internally; the upstream headers' std::wstring/HANDLE/FILETIME members only carry them around
// (HANDLE holds a file descriptor or a FindState*). CE names are case-insensitive, so each path component is
// matched against the directory when the exact name is missing.
#include "peripherals/cerf_virt/folder_share_dir.h"
#include "peripherals/cerf_virt/folder_share_files.h"
#include "peripherals/cerf_virt/folder_share_path.h"
#include "peripherals/cerf_virt/folder_share_stage.h"

#include "core/cerf_emulator.h"
#include "core/device_config.h"
#include "core/folder_share_config.h"
#include "core/log.h"
#include "core/string_utils.h"
#include "socs/guest_cpu_reset.h"
#include "state/state_stream.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using namespace CerfVirt;

REGISTER_SERVICE(FolderSharePath);
REGISTER_SERVICE(FolderShareDir);
REGISTER_SERVICE(FolderShareFiles);

namespace {

constexpr uint16_t kAttrReadonly = 0x01, kAttrDir = 0x10, kAttrArchive = 0x20;

std::string U16ToUtf8(const uint16_t* s, size_t n) {
    std::string o;
    for (size_t i = 0; i < n && s[i]; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | c >> 6); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | c >> 12); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | c >> 18); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    return o;
}

size_t Utf8ToU16(const std::string& u, uint16_t* out, size_t max) {
    size_t n = 0;
    for (size_t i = 0; i < u.size() && n + 2 < max;) {
        unsigned char c = (unsigned char)u[i];
        uint32_t cp; int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if (c >= 0xF0) { cp = c & 7; extra = 3; }
        else if (c >= 0xE0) { cp = c & 15; extra = 2; }
        else { cp = c & 31; extra = 1; }
        for (int k = 1; k <= extra && i + k < u.size(); k++) cp = (cp << 6) | ((unsigned char)u[i + k] & 63);
        i += 1 + extra;
        if (cp >= 0x10000) { cp -= 0x10000; out[n++] = (uint16_t)(0xD800 + (cp >> 10)); out[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF)); }
        else out[n++] = (uint16_t)cp;
    }
    return n;
}

std::string ToLowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

bool HasWild(const std::string& s) { return s.find_first_of("*?") != std::string::npos; }

// Case-insensitive glob; "*.*" matches every name like the Win32 find does.
bool Glob(const char* p, const char* n) {
    if (!strcmp(p, "*.*")) return true;  // Win32 find: "*.*" also matches names without a dot
    for (; *p; p++, n++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            if (!p[1]) return true;
            for (; *n; n++) if (Glob(p + 1, n)) return true;
            return Glob(p + 1, n);
        }
        if (!*n) return false;
        if (*p != '?' && ToLowerAscii(std::string(1, *p))[0] != ToLowerAscii(std::string(1, *n))[0]) return false;
    }
    return !*n;
}

uint32_t DosFromUnix(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    const int y = tmv.tm_year + 1900 < 1980 ? 0 : tmv.tm_year + 1900 - 1980;
    const uint32_t date = (uint32_t)((y << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
    const uint32_t tim = (uint32_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
    return (date << 16) | tim;
}

time_t UnixFromDos(uint32_t d) {
    struct tm tmv = {};
    const uint32_t date = d >> 16, tim = d & 0xFFFF;
    tmv.tm_year = (int)(date >> 9) + 80;
    tmv.tm_mon = (int)((date >> 5) & 15) - 1;
    tmv.tm_mday = (int)(date & 31);
    tmv.tm_hour = (int)(tim >> 11);
    tmv.tm_min = (int)((tim >> 5) & 63);
    tmv.tm_sec = (int)(tim & 31) * 2;
    tmv.tm_isdst = -1;
    return mktime(&tmv);
}

constexpr uint64_t kEpoch1601 = 11644473600ull;

uint16_t AttrsOf(const struct stat& st) {
    uint16_t a = S_ISDIR(st.st_mode) ? kAttrDir : kAttrArchive;
    if (!(st.st_mode & S_IWUSR)) a |= kAttrReadonly;
    return a;
}

struct Entry { std::string name; uint16_t attrs; uint32_t size; uint32_t mtime, ctime; };

Entry EntryFor(const std::string& name, const struct stat& st) {
    return {name, AttrsOf(st), st.st_size > 0xFFFFFFFFll ? 0xFFFFFFFFu : (uint32_t)st.st_size,
            DosFromUnix(st.st_mtime), DosFromUnix(st.st_ctime)};
}

// An open directory listing: all matches are collected up front, then handed out one per call.
struct FindState { std::vector<Entry> list; size_t next = 0; };

std::string U8(const std::wstring& w) { return WideToUtf8(w); }

void FillEntry(ServerPB& pb, const Entry& e) {
    pb.fFileAttributes = e.attrs;
    pb.fSize = e.size;
    pb.fFileTimeDate = e.mtime;
    pb.fFileCreateTimeDate = e.ctime;
    const size_t n = Utf8ToU16(e.name, pb.fLfn.fName, kMaxLfn);
    pb.fLfn.fName[n] = 0;
    pb.fLfn.fNameLength = (uint16_t)(n * sizeof(uint16_t));
}

}  // namespace

// ---- FolderSharePath -------------------------------------------------------------------------------------------

uint16_t FolderSharePath::ToWin32Path(const uint16_t* ce_name, uint16_t ce_len_bytes, std::wstring& out) const {
    size_t n = ce_len_bytes / sizeof(uint16_t);
    if (n > kMaxLfn) n = kMaxLfn;
    std::string root = U8(emu_.Get<FolderShareConfig>().HostRoot());
    while (!root.empty() && root.back() == '/') root.pop_back();
    if (root.empty()) return kErrorPathNotFound;

    std::string ce = U16ToUtf8(ce_name, n);
    for (char& c : ce) if (c == '\\') c = '/';
    std::vector<std::string> parts;
    for (size_t i = 0; i <= ce.size();) {
        size_t j = ce.find('/', i);
        if (j == std::string::npos) j = ce.size();
        std::string p = ce.substr(i, j - i);
        i = j + 1;
        if (p.empty() || p == ".") continue;
        if (p == "..") { if (parts.empty()) return kErrorInvalidName; parts.pop_back(); continue; }
        parts.push_back(p);
    }
    std::string cur = root;
    for (size_t k = 0; k < parts.size(); k++) {
        std::string next = cur + "/" + parts[k];
        struct stat st;
        if (!HasWild(parts[k]) && lstat(next.c_str(), &st) != 0) {
            // Exact name is missing: look for a case-insensitive twin so "\FOO.TXT" finds "foo.txt".
            if (DIR* d = opendir(cur.c_str())) {
                const std::string want = ToLowerAscii(parts[k]);
                while (dirent* de = readdir(d))
                    if (ToLowerAscii(de->d_name) == want) { next = cur + "/" + de->d_name; break; }
                closedir(d);
            }
        }
        cur = next;
    }
    out = Utf8ToWide(cur.c_str());
    return kErrorNoError;
}

uint32_t FolderSharePath::FiletimeToLong(const FILETIME& ft) {
    const uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return DosFromUnix((time_t)(t / 10000000ull - kEpoch1601));
}

bool FolderSharePath::LongToFiletime(uint32_t dos, FILETIME& out) {
    const uint64_t t = ((uint64_t)UnixFromDos(dos) + kEpoch1601) * 10000000ull;
    out.dwLowDateTime = (DWORD)t;
    out.dwHighDateTime = (DWORD)(t >> 32);
    return true;
}

uint16_t FolderSharePath::CeFileAttributes(DWORD a) { return (uint16_t)a; }
uint32_t FolderSharePath::CeFileSize(DWORD hi, DWORD lo) { return hi ? 0xFFFFFFFFu : lo; }

// `e` is an errno value (the shim's GetLastError()).
uint16_t FolderSharePath::CeError(DWORD e) {
    switch ((int)e) {
        case ENOENT: return kErrorFileNotFound;
        case ENOTDIR: return kErrorPathNotFound;
        case EMFILE: case ENFILE: return kErrorTooManyOpenFiles;
        case EACCES: case EPERM: case ENOTEMPTY: case EISDIR: case EBUSY: return kErrorAccessDenied;
        case EBADF: return kErrorInvalidHandle;
        case EROFS: return kErrorWriteProtect;
        case EIO: return kErrorWriteFault;
        case ENOSPC: case EDQUOT: return kErrorDiskFull;
        case EEXIST: return kErrorFileExists;
        case ENAMETOOLONG: case EINVAL: return kErrorInvalidName;
        default:
            LOG(Cerf, "[FolderShare] unmapped errno %d -> general failure\n", (int)e);
            return kErrorGeneralFailure;
    }
}

// ---- FolderShareDir --------------------------------------------------------------------------------------------

FolderShareDir::~FolderShareDir() { CloseAll(); }

bool FolderShareDir::ShouldRegister() { return emu_.Get<DeviceConfig>().guest_additions; }

void FolderShareDir::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { CloseAll(); });
}

void FolderShareDir::CloseAll() {
    for (auto& e : finds_) delete static_cast<FindState*>(e.second);
    finds_.clear();
}

void FolderShareDir::ReconcileGeneration() {
    const uint32_t g = emu_.Get<FolderShareConfig>().Generation();
    if (g != config_generation_) { config_generation_ = g; CloseAll(); }
}

bool FolderShareDir::Owns(uint32_t c) {
    return c == kServerMkDir || c == kServerRmDir || c == kServerSetAttributes || c == kServerRename ||
           c == kServerDelete || c == kServerGetInfo || c == kServerFindClose;
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

#define HOST_PATH(var, name, len)                                                              \
    std::wstring var##_w;                                                                      \
    if (uint16_t e_ = emu_.Get<FolderSharePath>().ToWin32Path(name, len, var##_w)) return e_;  \
    const std::string var = U8(var##_w)

uint16_t FolderShareDir::MkDir(ServerPB& pb) {
    HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
    if (mkdir(p.c_str(), 0777) == 0) return kErrorNoError;
    return errno == EEXIST ? kErrorAlreadyExists : FolderSharePath::CeError(errno);
}

uint16_t FolderShareDir::RmDir(ServerPB& pb) {
    HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
    return rmdir(p.c_str()) == 0 ? kErrorNoError : FolderSharePath::CeError(errno);
}

uint16_t FolderShareDir::Delete(ServerPB& pb) {
    HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
    struct stat st;
    if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return kErrorAccessDenied;  // DeleteFile refuses directories
    return unlink(p.c_str()) == 0 ? kErrorNoError : FolderSharePath::CeError(errno);
}

uint16_t FolderShareDir::Rename(ServerPB& pb) {
    HOST_PATH(src, pb.fLfn.fName, pb.fLfn.fNameLength);
    HOST_PATH(dst, pb.fLfn.fName2, pb.fLfn.fName2Length);
    struct stat st;
    if (lstat(dst.c_str(), &st) == 0) return kErrorAlreadyExists;  // MoveFile does not replace
    return rename(src.c_str(), dst.c_str()) == 0 ? kErrorNoError : FolderSharePath::CeError(errno);
}

uint16_t FolderShareDir::SetAttributes(ServerPB& pb) {
    HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
    if (pb.fFileTimeDate != 0) {
        const struct timespec ts[2] = {{0, UTIME_OMIT}, {UnixFromDos(pb.fFileTimeDate), 0}};
        if (utimensat(AT_FDCWD, p.c_str(), ts, 0) != 0) return FolderSharePath::CeError(errno);
    }
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return FolderSharePath::CeError(errno);
    const mode_t m = (pb.fFileAttributes & kAttrReadonly) ? (st.st_mode & ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH))
                                                           : (st.st_mode | S_IWUSR);
    if (m != st.st_mode && chmod(p.c_str(), m) != 0) return FolderSharePath::CeError(errno);
    return kErrorNoError;
}

uint16_t FolderShareDir::CloseFind(ServerPB& pb) {
    auto it = finds_.find(pb.fFindTransactionID);
    if (it != finds_.end()) { delete static_cast<FindState*>(it->second); finds_.erase(it); }
    return kErrorNoError;
}

uint16_t FolderShareDir::GetInfo(ServerPB& pb) {
    if (pb.fIndex == -1) {
        HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
        struct stat st;
        if (stat(p.c_str(), &st) != 0) return FolderSharePath::CeError(errno);
        const Entry e = EntryFor("", st);
        pb.fFileAttributes = e.attrs; pb.fSize = e.size; pb.fFileTimeDate = e.mtime; pb.fFileCreateTimeDate = e.ctime;
        return kErrorNoError;
    }
    const uint32_t tid = pb.fFindTransactionID;
    if (pb.fIndex == 0) {
        CloseFind(pb);
        HOST_PATH(spec, pb.fLfn.fName, pb.fLfn.fNameLength);
        const size_t slash = spec.rfind('/');
        const std::string dir = spec.substr(0, slash), pat = spec.substr(slash + 1);
        DIR* d = opendir(dir.c_str());
        if (!d) return FolderSharePath::CeError(errno);
        auto* fs = new FindState;
        while (dirent* de = readdir(d)) {
            const std::string n = de->d_name;
            if (n == "." || n == ".." || !Glob(pat.c_str(), n.c_str())) continue;
            struct stat st;
            if (stat((dir + "/" + n).c_str(), &st) == 0) fs->list.push_back(EntryFor(n, st));
        }
        closedir(d);
        if (fs->list.empty()) { delete fs; return kErrorNoMoreFiles; }
        finds_[tid] = fs;
    }
    auto it = finds_.find(tid);
    if (it == finds_.end()) return kErrorNoMoreFiles;
    auto* fs = static_cast<FindState*>(it->second);
    if (fs->next >= fs->list.size()) { delete fs; finds_.erase(it); return kErrorNoMoreFiles; }
    FillEntry(pb, fs->list[fs->next++]);
    return kErrorNoError;
}

// ---- FolderShareFiles ------------------------------------------------------------------------------------------

namespace { int Fd(HANDLE h) { return (int)(intptr_t)h; } }

FolderShareFiles::~FolderShareFiles() { CloseAll(); }

bool FolderShareFiles::ShouldRegister() { return emu_.Get<DeviceConfig>().guest_additions; }

void FolderShareFiles::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { CloseAll(); });
}

void FolderShareFiles::CloseAll() {
    for (auto& e : files_) close(Fd(e.second.h));
    files_.clear();
}

void FolderShareFiles::ReconcileGeneration() {
    const uint32_t g = emu_.Get<FolderShareConfig>().Generation();
    if (g != config_generation_) { config_generation_ = g; CloseAll(); }
}

void FolderShareFiles::SaveState(StateWriter& w) { w.Write("next_handle", next_handle_); }

void FolderShareFiles::RestoreState(StateReader& r) {
    r.Read("next_handle", next_handle_);
    CloseAll();
}

FolderShareFiles::OpenFile* FolderShareFiles::FileFor(uint16_t handle) {
    auto it = files_.find(handle);
    return it == files_.end() ? nullptr : &it->second;
}

bool FolderShareFiles::Owns(uint32_t c) {
    return c == kServerGetDriveConfig || c == kServerCreate || c == kServerOpen || c == kServerRead ||
           c == kServerWrite || c == kServerSetEOF || c == kServerClose || c == kServerGetSpace ||
           c == kServerGetFCBInfo || c == kServerGetMaxIOSize;
}

uint32_t FolderShareFiles::Run(uint32_t code, ServerPB& pb) {
    switch (code) {
        case kServerGetDriveConfig: return GetDriveConfig(pb);
        case kServerCreate:         return Create(pb);
        case kServerOpen:           return Open(pb);
        case kServerRead:           return Read(pb);
        case kServerWrite:          return Write(pb);
        case kServerSetEOF:         return SetEof(pb);
        case kServerClose:          return Close(pb);
        case kServerGetSpace:       return GetSpace(pb);
        case kServerGetFCBInfo:     return GetFcbInfo(pb);
        case kServerGetMaxIOSize:
            LOG(Cerf, "[FolderShareFiles] GET_MAX_IO_SIZE has no issuing client\n");
            CerfFatalExit();
        default: break;
    }
    LOG(Cerf, "[FolderShareFiles] Run called with unowned op 0x%X\n", code);
    CerfFatalExit();
}

bool FolderShareFiles::AllocHandle(uint16_t& handle) {
    if (files_.size() > 0xFFFFu) return false;
    do { next_handle_ = (uint16_t)(next_handle_ + 1u); } while (files_.count(next_handle_) != 0);
    handle = next_handle_;
    return true;
}

uint16_t FolderShareFiles::GetDriveConfig(ServerPB&) {
    LOG(Cerf, "[FolderShareFiles] GET_DRIVE_CONFIG has no issuing client\n");
    CerfFatalExit();
}

uint16_t FolderShareFiles::Create(ServerPB& pb) {
    HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
    const int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) return FolderSharePath::CeError(errno);
    close(fd);
    return kErrorNoError;
}

uint16_t FolderShareFiles::Open(ServerPB& pb) {
    HOST_PATH(p, pb.fLfn.fName, pb.fLfn.fNameLength);
    uint16_t handle;
    if (!AllocHandle(handle)) return kErrorTooManyOpenFiles;
    int flags;
    switch (pb.fOpenMode & 0x03u) {
        case kOpenAccessReadOnly:  flags = O_RDONLY; break;
        case kOpenAccessWriteOnly: flags = O_WRONLY; break;
        case kOpenAccessReadWrite: flags = O_RDWR; break;
        default:
            LOG(Cerf, "[FolderShareFiles] Open with undefined access mode 0x%X\n", pb.fOpenMode);
            CerfFatalExit();
    }
    // Share modes are not enforced: one host user, and CE apps rarely rely on denial.
    const int fd = open(p.c_str(), flags | O_CLOEXEC);
    if (fd < 0) return FolderSharePath::CeError(errno);
    OpenFile& f = files_[handle];
    f.h = (HANDLE)(intptr_t)fd;
    f.name.clear();
    for (uint16_t i = 0, n = pb.fLfn.fNameLength / 2; i < n; i++) f.name += (wchar_t)pb.fLfn.fName[i];
    pb.fHandle = handle;
    return kErrorNoError;
}

uint16_t FolderShareFiles::Read(ServerPB& pb) {
    OpenFile* f = FileFor(pb.fHandle);
    if (!f) return kErrorInvalidHandle;
    if (pb.fSize > kFolderShareMaxReadWriteSize) { LOG(Cerf, "[FolderShareFiles] Read of %u bytes exceeds the stage\n", pb.fSize); CerfFatalExit(); }
    const ssize_t n = pread(Fd(f->h), emu_.Get<FolderShareStage>().IoBuf(), pb.fSize, pb.fPosition);
    if (n < 0) return FolderSharePath::CeError(errno);
    pb.fSize = (uint32_t)n;
    return kErrorNoError;
}

uint16_t FolderShareFiles::Write(ServerPB& pb) {
    OpenFile* f = FileFor(pb.fHandle);
    if (!f) return kErrorInvalidHandle;
    if (pb.fSize > kFolderShareMaxReadWriteSize) { LOG(Cerf, "[FolderShareFiles] Write of %u bytes exceeds the stage\n", pb.fSize); CerfFatalExit(); }
    const ssize_t n = pwrite(Fd(f->h), emu_.Get<FolderShareStage>().IoBuf(), pb.fSize, pb.fPosition);
    if (n < 0) return FolderSharePath::CeError(errno);
    pb.fSize = (uint32_t)n;
    return kErrorNoError;
}

uint16_t FolderShareFiles::SetEof(ServerPB& pb) {
    OpenFile* f = FileFor(pb.fHandle);
    if (!f) return kErrorInvalidHandle;
    return ftruncate(Fd(f->h), pb.fPosition) == 0 ? kErrorNoError : FolderSharePath::CeError(errno);
}

uint16_t FolderShareFiles::Close(ServerPB& pb) {
    auto it = files_.find(pb.fHandle);
    if (it == files_.end()) return kErrorInvalidHandle;
    close(Fd(it->second.h));
    files_.erase(it);
    return kErrorNoError;
}

uint16_t FolderShareFiles::GetFcbInfo(ServerPB& pb) {
    OpenFile* f = FileFor(pb.fHandle);
    if (!f) return kErrorInvalidHandle;
    struct stat st;
    if (fstat(Fd(f->h), &st) != 0) return FolderSharePath::CeError(errno);
    const Entry e = EntryFor("", st);
    pb.fFileAttributes = e.attrs; pb.fSize = e.size; pb.fFileTimeDate = e.mtime; pb.fFileCreateTimeDate = e.ctime;
    size_t n = f->name.size();
    if (n > kMaxLfn) n = kMaxLfn;
    for (size_t i = 0; i < n; i++) pb.fLfn.fName[i] = (uint16_t)f->name[i];
    pb.fLfn.fName[n] = 0;
    pb.fLfn.fNameLength = (uint16_t)(n * sizeof(uint16_t));
    return kErrorNoError;
}

uint16_t FolderShareFiles::GetSpace(ServerPB& pb) {
    const std::string root = U8(emu_.Get<FolderShareConfig>().HostRoot());
    struct statvfs sv;
    if (statvfs(root.c_str(), &sv) != 0) return FolderSharePath::CeError(errno);
    const uint64_t cluster = 512ull * 64ull;
    uint64_t total = (uint64_t)sv.f_blocks * sv.f_frsize / cluster, avail = (uint64_t)sv.f_bavail * sv.f_frsize / cluster;
    if (total > 0xFFFFFFFFull) total = 0xFFFFFFFFull;
    if (avail > 0xFFFFFFFFull) avail = 0xFFFFFFFFull;
    pb.fPosition = (uint32_t)total;
    pb.fSize = (uint32_t)avail;
    return kErrorNoError;
}
