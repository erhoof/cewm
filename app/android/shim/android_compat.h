// Android compat, force-included. NDK r27's libc++ (clang 18) lacks std::atomic_ref; CERF uses it for uint32_t flags only. Force-included in the Android build.
#pragma once
// bionic defines PAGE_SIZE as a macro, which clashes with CERF's EmulatedMemory::PAGE_SIZE.
#include <limits.h>
#include <sys/user.h>
#undef PAGE_SIZE
#include <atomic>
#include <cstddef>
namespace std {
template <class T> struct atomic_ref {
    static constexpr size_t required_alignment = alignof(T);
    explicit atomic_ref(T& r) : p_(&r) {}
    T load(memory_order o = memory_order_seq_cst) const noexcept { return __atomic_load_n(p_, (int)o); }
    void store(T v, memory_order o = memory_order_seq_cst) const noexcept { __atomic_store_n(p_, v, (int)o); }
    T exchange(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_exchange_n(p_, v, (int)o); }
    T fetch_or(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_or(p_, v, (int)o); }
    T fetch_and(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_and(p_, v, (int)o); }
    T fetch_add(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_add(p_, v, (int)o); }
    bool compare_exchange_strong(T& e, T d, memory_order o = memory_order_seq_cst) const noexcept {
        return __atomic_compare_exchange_n(p_, &e, d, false, (int)o, (int)o == (int)memory_order_acq_rel ? (int)memory_order_acquire : (int)o);
    }
  private:
    T* p_;
};
}  // namespace std
