// Host HTTP proxy that upgrades guest http:// requests to https:// (see http_proxy.cpp).
#pragma once
namespace cewm {
// Listens on 127.0.0.1:port (0 = any); returns the bound port, or -1.
int StartHttpProxy(int port);
}
