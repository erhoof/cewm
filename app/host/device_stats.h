// Polls Aurora system D-Bus (MCE battery, ofono operator, connman wifi) into cewm::Host(). No-op without libdbus (laptop).
#pragma once
namespace cewm {
void StartDeviceStats();
}
