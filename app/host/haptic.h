// Short vibration tick: ngfd on the Aurora system bus, the Vibrator service on Android. No-op without libdbus or ngfd.
#pragma once
namespace cewm {
void HapticTick();
}
