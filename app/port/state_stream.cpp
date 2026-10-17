// POSIX fork of cerf/cerf/state/state_stream.cpp (MIT): same on-disk layout, fds instead of Win32 handles. Save-side writes
// "<path>.tmp" and renames on Commit.
#include "core/cerf_emulator.h"
#include "core/log.h"
#include "state/state_stream.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {
std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }  // paths here are ASCII/UTF-8 bytes widened by the app
int FdOf(HANDLE h) { return static_cast<int>(reinterpret_cast<intptr_t>(h)); }
HANDLE HandleOf(int fd) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)); }
const char* FieldKindName(uint8_t kind) {
    switch (kind) {
        case kStateFieldPod:   return "a value";
        case kStateFieldBytes: return "a byte block";
        default:               return "an unknown field kind";
    }
}
}  // namespace

StateWriter::StateWriter(const std::wstring& path) : final_path_(path), temp_path_(path + L".tmp") {
    const int fd = open(Narrow(temp_path_).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { Fail("the state file cannot be created", errno); return; }
    file_ = HandleOf(fd);
    ok_ = true;
}

void StateWriter::Fail(const char* what, DWORD err) {
    ok_ = false;
    if (!error_.empty()) return;
    error_ = err ? std::string(what) + ": " + strerror(static_cast<int>(err)) : std::string(what);
}

StateWriter::~StateWriter() {
    CloseHandle_();
    if (!committed_) unlink(Narrow(temp_path_).c_str());
}

void StateWriter::CloseHandle_() {
    if (file_ != INVALID_HANDLE_VALUE) { close(FdOf(file_)); file_ = INVALID_HANDLE_VALUE; }
}

void StateWriter::WriteRaw(const void* src, size_t n) {
    if (!ok_) return;
    const auto* p = static_cast<const uint8_t*>(src);
    while (n > 0) {
        const ssize_t w = write(FdOf(file_), p, n);
        if (w <= 0) { Fail("a write to the state file failed", errno); return; }
        p += w; n -= static_cast<size_t>(w); bytes_written_ += static_cast<uint64_t>(w);
    }
}

void StateWriter::WriteTag(uint8_t kind, const char* name, uint64_t size) {
    const StateFieldTag tag{kind, StateFieldNameHash(name), size};
    WriteRaw(&tag, sizeof(tag));
}

void StateWriter::WriteBlock(const char* name, const void* src, size_t n) {
    WriteTag(kStateFieldBytes, name, n);
    if (n) WriteRaw(src, n);
}

void StateWriter::BeginFrame(uint32_t id) {
    frames_.push_back(bytes_written_);
    const StateFrameHeader header{id, 0};
    WriteRaw(&header, sizeof(header));
}

void StateWriter::EndFrame() {
    if (frames_.empty()) { LOG(Caution, "StateWriter: EndFrame with no open frame\n"); CerfFatalExit(CERF_FATAL_RUNTIME_ERROR); }
    const uint64_t header_off = frames_.back();
    frames_.pop_back();
    const uint64_t length = bytes_written_ - header_off - sizeof(StateFrameHeader);
    PatchAt(header_off + offsetof(StateFrameHeader, length), &length, sizeof(length));
}

void StateWriter::PatchAt(uint64_t offset, const void* src, size_t n) {
    if (!ok_) return;
    if (pwrite(FdOf(file_), src, n, static_cast<off_t>(offset)) != static_cast<ssize_t>(n)) Fail("a write to the state file failed", errno);
}

bool StateWriter::Commit() {
    if (!frames_.empty()) { LOG(Caution, "StateWriter: Commit with %zu open frames\n", frames_.size()); CerfFatalExit(CERF_FATAL_RUNTIME_ERROR); }
    if (!ok_) { CloseHandle_(); unlink(Narrow(temp_path_).c_str()); return false; }
    if (fsync(FdOf(file_)) != 0) Fail("the state file cannot be flushed", errno);
    CloseHandle_();
    if (!ok_ || rename(Narrow(temp_path_).c_str(), Narrow(final_path_).c_str()) != 0) {
        Fail("the state file cannot be replaced", errno);
        unlink(Narrow(temp_path_).c_str());
        return false;
    }
    committed_ = true;
    return true;
}

StateReader::StateReader(const std::wstring& path) {
    const int fd = open(Narrow(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) { error_ = std::string("the state file cannot be opened: ") + strerror(errno); return; }
    file_ = HandleOf(fd);
    struct stat st;
    if (fstat(fd, &st) != 0) { error_ = std::string("the state file cannot be read: ") + strerror(errno); return; }
    file_size_ = static_cast<uint64_t>(st.st_size);
    ok_ = true;
}

StateReader::~StateReader() { if (file_ != INVALID_HANDLE_VALUE) close(FdOf(file_)); }

void StateReader::Reject(const char* fmt, ...) {
    ok_ = false;
    char reason[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);
    std::string where = "image";
    for (const Frame& f : frames_) { char id[16]; snprintf(id, sizeof(id), "/0x%X", f.id); where += id; }
    char field[96];
    snprintf(field, sizeof(field), " field %u '%s'", fields_, field_name_.c_str());
    throw StateImageRejected(where + field + ": " + reason);
}

uint64_t StateReader::Remaining() const {
    const uint64_t limit = frames_.empty() ? file_size_ : frames_.back().end;
    return limit > pos_ ? limit - pos_ : 0;
}

void StateReader::ReadRaw(void* dst, size_t n) {
    if (!ok_) Reject("the image is not open");
    if (n > Remaining()) Reject("the saved data ends %llu bytes short of what this build reads", static_cast<unsigned long long>(n - Remaining()));
    auto* p = static_cast<uint8_t*>(dst);
    while (n > 0) {
        const ssize_t got = pread(FdOf(file_), p, n, static_cast<off_t>(pos_));
        if (got <= 0) Reject("read failed errno=%d", errno);
        p += got; n -= static_cast<size_t>(got); pos_ += static_cast<uint64_t>(got);
    }
}

void StateReader::ExpectTag(uint8_t kind, const char* name, uint64_t size) {
    ++fields_;
    field_name_ = name;
    StateFieldTag tag{};
    ReadRaw(&tag, sizeof(tag));
    if (tag.name != StateFieldNameHash(name)) Reject("the saved field at this position is a different field");
    if (tag.kind != kind || tag.size != size)
        Reject("saved %s of %llu bytes, this build reads %s of %llu bytes", FieldKindName(tag.kind), static_cast<unsigned long long>(tag.size),
               FieldKindName(kind), static_cast<unsigned long long>(size));
}

void StateReader::ReadBlock(const char* name, void* dst, size_t n) {
    ExpectTag(kStateFieldBytes, name, n);
    if (n) ReadRaw(dst, n);
}

uint32_t StateReader::EnterFrame() {
    StateFrameHeader header{};
    ReadRaw(&header, sizeof(header));
    if (header.length > Remaining()) Reject("frame 0x%X claims %llu bytes past the end of its parent", header.id, static_cast<unsigned long long>(header.length - Remaining()));
    frames_.push_back(Frame{header.id, pos_ + header.length, fields_});
    fields_ = 0;
    field_name_ = "";
    return header.id;
}

void StateReader::LeaveFrame() {
    if (frames_.empty()) { LOG(Caution, "StateReader: LeaveFrame with no open frame\n"); CerfFatalExit(CERF_FATAL_RUNTIME_ERROR); }
    if (Remaining() != 0) Reject("this build leaves %llu saved bytes unread", static_cast<unsigned long long>(Remaining()));
    fields_ = frames_.back().parent_fields;
    field_name_ = "";
    frames_.pop_back();
}

void StateReader::SkipFrame() {
    if (frames_.empty()) { LOG(Caution, "StateReader: SkipFrame with no open frame\n"); CerfFatalExit(CERF_FATAL_RUNTIME_ERROR); }
    SeekTo(frames_.back().end);
    fields_ = frames_.back().parent_fields;
    field_name_ = "";
    frames_.pop_back();
}

void StateReader::SeekTo(uint64_t offset) { pos_ = offset; }
