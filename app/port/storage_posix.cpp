// POSIX forks of cerf/cerf/storage/mapped_file.cpp and disk_image.cpp (MIT). MappedFile maps the whole file: unlike upstream's
// 2 GB-address-space sliding window, a 64-bit host has room (shortcut: a multi-GiB ROM/image is mapped in full, lazily paged).
#include "core/log.h"
#include "storage/disk_image.h"
#include "storage/mapped_file.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "cerf_path.h"
#include <unistd.h>

#include <cstring>

static int FdOf(HANDLE h) { return static_cast<int>(reinterpret_cast<intptr_t>(h)); }
static HANDLE HandleOf(int fd) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)); }

MappedFile::~MappedFile() {
    if (view_) munmap(view_, view_len_);
    if (file_ != INVALID_HANDLE_VALUE) close(FdOf(file_));
}

bool MappedFile::Open(const std::string& path) {
    const int fd = open(CerfPosixPath(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOG(Caution, "MappedFile: open('%s') failed errno=%d\n", path.c_str(), errno);
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return false;
    }
    void* base = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        return false;
    }
    file_ = HandleOf(fd);
    mapping_ = reinterpret_cast<HANDLE>(1);  // IsOpen() marker
    view_ = static_cast<uint8_t*>(base);
    view_base_ = 0;
    view_len_ = static_cast<size_t>(st.st_size);
    size_ = static_cast<uint64_t>(st.st_size);
    return true;
}

bool MappedFile::EnsureWindow(uint64_t offset, size_t len) { return offset + len <= size_; }

const uint8_t* MappedFile::View(uint64_t offset, size_t len) {
    return view_ && offset + len <= size_ ? view_ + offset : nullptr;
}

size_t MappedFile::Read(uint64_t offset, void* dst, size_t len) {
    if (!view_ || offset >= size_) return 0;
    const size_t n = len < size_ - offset ? len : static_cast<size_t>(size_ - offset);
    memcpy(dst, view_ + offset, n);
    return n;
}

DiskImage::~DiskImage() {
    if (handle_ != INVALID_HANDLE_VALUE) close(FdOf(handle_));
}

bool DiskImage::Open(const std::string& path, uint64_t size_bytes, bool create_if_missing) {
    const int fd = open(CerfPosixPath(path).c_str(), O_RDWR | O_CLOEXEC | (create_if_missing ? O_CREAT : 0), 0644);
    if (fd < 0) {
        LOG(Caution, "DiskImage: open('%s') failed errno=%d\n", path.c_str(), errno);
        return false;
    }
    handle_ = HandleOf(fd);
    struct stat st;
    const uint64_t physical = fstat(fd, &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
    // Same rule as upstream: advertise max(logical, physical); the file is never pre-extended and grows on write.
    sector_count_ = (physical > size_bytes ? physical : size_bytes) / kSectorSize;
    LOG(Boot, "DiskImage: '%s' open, %llu sectors\n", path.c_str(), static_cast<unsigned long long>(sector_count_));
    return true;
}

bool DiskImage::SeekTo(uint64_t lba) { return lseek(FdOf(handle_), static_cast<off_t>(lba * kSectorSize), SEEK_SET) >= 0; }

bool DiskImage::ReadSectors(uint64_t lba, uint32_t count, void* dst) {
    if (handle_ == INVALID_HANDLE_VALUE || lba + count > sector_count_) return false;
    const uint64_t off = lba * kSectorSize;
    const size_t want = static_cast<size_t>(count) * kSectorSize;
    auto* out = static_cast<uint8_t*>(dst);
    struct stat st;
    if (fstat(FdOf(handle_), &st) != 0) return false;
    const uint64_t psize = static_cast<uint64_t>(st.st_size);
    if (off >= psize) { memset(out, 0, want); return true; }  // unwritten tail reads as zero
    const size_t in_file = psize - off >= want ? want : static_cast<size_t>(psize - off);
    if (pread(FdOf(handle_), out, in_file, static_cast<off_t>(off)) != static_cast<ssize_t>(in_file)) return false;
    if (in_file < want) memset(out + in_file, 0, want - in_file);
    return true;
}

bool DiskImage::WriteSectors(uint64_t lba, uint32_t count, const void* src) {
    if (handle_ == INVALID_HANDLE_VALUE || lba + count > sector_count_) return false;
    const size_t want = static_cast<size_t>(count) * kSectorSize;
    return pwrite(FdOf(handle_), src, want, static_cast<off_t>(lba * kSectorSize)) == static_cast<ssize_t>(want);
}

bool DiskImage::PunchHole(uint64_t lba, uint32_t count) {
    if (handle_ == INVALID_HANDLE_VALUE || lba + count > sector_count_) return false;
    return fallocate(FdOf(handle_), FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, static_cast<off_t>(lba * kSectorSize),
                     static_cast<off_t>(count) * kSectorSize) == 0;
}
