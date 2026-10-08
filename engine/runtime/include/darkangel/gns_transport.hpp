#pragma once
#include <darkangel/transport.hpp>
namespace darkangel {
// Offline LAN: no EOS initialization or login. One adapter owns one peer.
std::unique_ptr<Transport> create_gns_host(std::string bind_address,SessionHandshake,TransportLimits={});
std::unique_ptr<Transport> create_gns_client(std::string host_address,SessionHandshake,TransportLimits={});
}
