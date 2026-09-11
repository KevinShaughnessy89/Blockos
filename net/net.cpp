#include "net.hpp"
#include "arp.hpp"
#include "udp.hpp"
#include "tcp.hpp"
#include "icmp.hpp"
#include "ipv4.hpp"
#include "../drivers/virtio_net_driver.hpp"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace blockos::net {

namespace {

MacAddress g_mac{};
IPv4Address g_ip{{10, 0, 2, 15}};
IPv4Address g_mask{{255, 255, 255, 0}};
IPv4Address g_gw{{10, 0, 2, 2}};

bool g_initialized = false;

} // namespace


// ------------------------------------------------------------
// MAC address
// ------------------------------------------------------------

MacAddress mac_address()
{
    return g_mac;
}

void set_mac_address(const uint8_t mac[6])
{
    if (mac == nullptr)
        return;

    memcpy(g_mac.b, mac, 6);
}


// ------------------------------------------------------------
// IPv4 configuration
// ------------------------------------------------------------

IPv4Address ip_address()
{
    return g_ip;
}

IPv4Address netmask()
{
    return g_mask;
}

IPv4Address gateway()
{
    return g_gw;
}

void set_ip_address(const IPv4Address& ip)
{
    g_ip = ip;
}

void set_netmask(const IPv4Address& mask)
{
    g_mask = mask;
}

void set_gateway(const IPv4Address& gw)
{
    g_gw = gw;
}


// ------------------------------------------------------------
// Endian helpers
// ------------------------------------------------------------

uint16_t htons(uint16_t x)
{
    return static_cast<uint16_t>(
        ((x & 0x00FFu) << 8) |
        ((x & 0xFF00u) >> 8)
    );
}

uint16_t ntohs(uint16_t x)
{
    return htons(x);
}

uint32_t htonl(uint32_t x)
{
    return
        ((x & 0x000000FFu) << 24) |
        ((x & 0x0000FF00u) << 8)  |
        ((x & 0x00FF0000u) >> 8)  |
        ((x & 0xFF000000u) >> 24);
}

uint32_t ntohl(uint32_t x)
{
    return htonl(x);
}


// ------------------------------------------------------------
// Network initialization
// ------------------------------------------------------------

bool init()
{
    if (g_initialized)
        return true;

    // --------------------------------------------------------
    // Initialize VirtIO network device
    // --------------------------------------------------------

    if (!virtio_net::init())
        return false;

    // --------------------------------------------------------
    // Get MAC address from VirtIO-net
    // --------------------------------------------------------

    uint8_t mac[6]{};

    if (!virtio_net::get_mac_address(mac))
        return false;

    set_mac_address(mac);

    // --------------------------------------------------------
    // Initialize protocol layers
    // --------------------------------------------------------

    arp_init();
    udp_init();
    tcp_init();

    g_initialized = true;

    return true;
}


// ------------------------------------------------------------
// Network polling
// ------------------------------------------------------------

void poll()
{
    if (!g_initialized)
        return;

    // Process packets received from the network device.
    //
    // virtio_net::receive_packet() returns:
    //   > 0  = packet received
    //   -1   = no packet available / error
    //
    // The VirtIO driver is responsible for putting received
    // Ethernet frames into the supplied buffer.

    uint8_t packet[2048];

    for (;;)
    {
        int received = virtio_net::receive_packet(
            packet,
            sizeof(packet)
        );

        if (received <= 0)
            break;

        ethernet_receive(
            packet,
            static_cast<size_t>(received)
        );
    }

    // Run TCP timers/retransmission handling.
    tcp_tick();
}


// ------------------------------------------------------------
// Main packet entry point
// ------------------------------------------------------------

void receive_packet(const void* packet, size_t len)
{
    if (packet == nullptr || len == 0)
        return;

    ethernet_receive(packet, len);
}


// ------------------------------------------------------------
// Send raw Ethernet frame
// ------------------------------------------------------------

bool ethernet_send(
    const MacAddress& destination,
    uint16_t ethertype,
    const void* payload,
    size_t payload_len
)
{
    if (payload == nullptr && payload_len != 0)
        return false;

    // Ethernet:
    //
    //   destination MAC  6 bytes
    //   source MAC       6 bytes
    //   EtherType        2 bytes
    //   payload          N bytes
    //
    // Maximum normal Ethernet frame:
    //   1514 bytes without FCS.

    constexpr size_t ETH_HEADER_SIZE = 14;
    constexpr size_t ETH_MAX_FRAME   = 1514;

    if (payload_len + ETH_HEADER_SIZE > ETH_MAX_FRAME)
        return false;

    uint8_t frame[ETH_MAX_FRAME];

    // Destination MAC
    memcpy(frame + 0, destination.b, 6);

    // Source MAC
    memcpy(frame + 6, g_mac.b, 6);

    // EtherType is network byte order.
    frame[12] = static_cast<uint8_t>((ethertype >> 8) & 0xFF);
    frame[13] = static_cast<uint8_t>(ethertype & 0xFF);

    if (payload_len != 0)
        memcpy(frame + ETH_HEADER_SIZE, payload, payload_len);

    return virtio_net::send_packet(
        frame,
        ETH_HEADER_SIZE + payload_len
    );
}


// ------------------------------------------------------------
// Network state
// ------------------------------------------------------------

bool is_initialized()
{
    return g_initialized;
}

} // namespace blockos::net
