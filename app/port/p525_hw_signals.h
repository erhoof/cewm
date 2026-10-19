#pragma once
#include <atomic>
// Counts releases of the GPRS_RESET line (PCA9535 0x42, port 0 bit 4) so the fake modem can boot when the guest resets it.
inline std::atomic<int> g_p525_gprs_reset_release{0};
