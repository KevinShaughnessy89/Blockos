#include "ethernet.hpp"
#include "arp.hpp"
#include "ipv4.hpp"

namespace blockos::net {

void ethernet_receive(const void* frame, size_t len) {
    if (!frame || len < sizeof(EthernetHeader)) return;
    const auto* h = static_cast<const EthernetHeader*>(frame);
    const uint16_t type = ntohs(h->type);
    const uint8_t* payload = static_cast<const uint8_t*>(frame) + sizeof(EthernetHeader);
    const size_t plen = len - sizeof(EthernetHeader);

    switch (type) {
        case ETHERTYPE_ARP:  arp_receive(payload, plen); break;
        case ETHERTYPE_IPV4: ipv4_receive(payload, plen); break;
        default: break;
    }
}

} // namespace blockos::net
