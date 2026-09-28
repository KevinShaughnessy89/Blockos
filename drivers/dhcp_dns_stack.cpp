#include "dhcp_dns_stack.hpp"
#include "virtio_net_driver.hpp"
#include "../net/net.hpp"
#include "../net/udp.hpp"
#include <string.h>

namespace {

#pragma pack(push,1)
struct DhcpWireHeader {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic_cookie;
};
#pragma pack(pop)

static constexpr uint32_t DHCP_MAGIC = 0x63825363u;
static constexpr uint16_t DHCP_CLIENT_PORT = 68;
static constexpr uint16_t DHCP_SERVER_PORT = 67;
static constexpr uint8_t DHCP_DISCOVER = 1;
static constexpr uint8_t DHCP_OFFER = 2;
static constexpr uint8_t DHCP_REQUEST = 3;
static constexpr uint8_t DHCP_ACK = 5;
static constexpr uint8_t DHCP_OPTION_SUBNET = 1;
static constexpr uint8_t DHCP_OPTION_ROUTER = 3;
static constexpr uint8_t DHCP_OPTION_DNS = 6;
static constexpr uint8_t DHCP_OPTION_SERVER = 54;
static constexpr uint8_t DHCP_OPTION_MESSAGE = 53;
static constexpr uint8_t DHCP_OPTION_REQUESTED_IP = 50;
static constexpr uint8_t DHCP_OPTION_END = 255;

static bool option_value(const uint8_t* opts, size_t len, uint8_t wanted, uint8_t* out, uint8_t* out_len)
{
    size_t p = 0;
    while (p < len) {
        const uint8_t tag = opts[p++];
        if (tag == 0) continue;
        if (tag == DHCP_OPTION_END) break;
        if (p >= len) return false;
        const uint8_t n = opts[p++];
        if (p + n > len) return false;
        if (tag == wanted) {
            if (out && out_len && *out_len >= n) {
                memcpy(out, opts + p, n);
                *out_len = n;
                return true;
            }
            return false;
        }
        p += n;
    }
    return false;
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

} // namespace

DhcpDnsEngine* DhcpDnsEngine::active = nullptr;

DhcpDnsEngine::DhcpDnsEngine()
    : transaction_id(0x39A341B2), assigned_ip(0), subnet_mask(0),
      gateway_ip(0), dns_server_ip(0), server_ip(0), client_mac{},
      state(0), dhcp_success(false)
{
}

void DhcpDnsEngine::udp_callback(const blockos::net::UdpDatagram& d)
{
    if (!active || d.dst_port != DHCP_CLIENT_PORT)
        return;
    active->on_packet(d.data, d.length);
}

void DhcpDnsEngine::send_dhcp_discover()
{
    if (!active)
        active = this;

    if (!virtio_net::get_mac_address(client_mac))
        return;

    uint8_t packet[548]{};
    DhcpWireHeader* h = reinterpret_cast<DhcpWireHeader*>(packet);
    h->op = 1;
    h->htype = 1;
    h->hlen = 6;
    h->xid = blockos::net::htonl(transaction_id);
    h->flags = blockos::net::htons(0x8000);
    memcpy(h->chaddr, client_mac, 6);
    h->magic_cookie = blockos::net::htonl(DHCP_MAGIC);

    uint8_t* o = packet + sizeof(DhcpWireHeader);
    *o++ = DHCP_OPTION_MESSAGE; *o++ = 1; *o++ = DHCP_DISCOVER;
    *o++ = DHCP_OPTION_END;

    blockos::net::IPv4Address broadcast{{255,255,255,255}};
    blockos::net::udp_send(broadcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                           packet, size_t(o - packet));
}

void DhcpDnsEngine::send_request(uint32_t requested_ip, uint32_t server)
{
    uint8_t packet[548]{};
    DhcpWireHeader* h = reinterpret_cast<DhcpWireHeader*>(packet);
    h->op = 1;
    h->htype = 1;
    h->hlen = 6;
    h->xid = blockos::net::htonl(transaction_id);
    h->flags = blockos::net::htons(0x8000);
    memcpy(h->chaddr, client_mac, 6);
    h->magic_cookie = blockos::net::htonl(DHCP_MAGIC);

    uint8_t* o = packet + sizeof(DhcpWireHeader);
    *o++ = DHCP_OPTION_MESSAGE; *o++ = 1; *o++ = DHCP_REQUEST;
    *o++ = DHCP_OPTION_REQUESTED_IP; *o++ = 4; put_be32(o, requested_ip); o += 4;
    *o++ = DHCP_OPTION_SERVER; *o++ = 4; put_be32(o, server); o += 4;
    *o++ = DHCP_OPTION_END;

    blockos::net::IPv4Address broadcast{{255,255,255,255}};
    blockos::net::udp_send(broadcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                           packet, size_t(o - packet));
}

void DhcpDnsEngine::on_packet(const uint8_t* data, size_t len)
{
    if (!data || len < sizeof(DhcpWireHeader))
        return;

    const DhcpWireHeader* h = reinterpret_cast<const DhcpWireHeader*>(data);
    if (h->op != 2 || blockos::net::ntohl(h->xid) != transaction_id ||
        blockos::net::ntohl(h->magic_cookie) != DHCP_MAGIC)
        return;
    if (memcmp(h->chaddr, client_mac, 6) != 0)
        return;

    const uint8_t* opts = data + sizeof(DhcpWireHeader);
    const size_t opts_len = len - sizeof(DhcpWireHeader);
    uint8_t msg = 0;
    uint8_t msg_len = 1;
    option_value(opts, opts_len, DHCP_OPTION_MESSAGE, &msg, &msg_len);

    uint8_t tmp[4];
    uint8_t tmp_len = sizeof(tmp);

    if (msg == DHCP_OFFER && state == 1) {
        assigned_ip = blockos::net::ntohl(h->yiaddr);
        server_ip = h->siaddr ? blockos::net::ntohl(h->siaddr) : 0;
        tmp_len = 4;
        if (option_value(opts, opts_len, DHCP_OPTION_SERVER, tmp, &tmp_len))
            server_ip = be32(tmp);
        send_request(assigned_ip, server_ip);
        state = 2;
        return;
    }

    if (msg == DHCP_ACK && (state == 2 || state == 1)) {
        assigned_ip = blockos::net::ntohl(h->yiaddr);

        tmp_len = 4;
        if (option_value(opts, opts_len, DHCP_OPTION_SUBNET, tmp, &tmp_len))
            subnet_mask = be32(tmp);
        else
            subnet_mask = 0xFFFFFF00u;

        tmp_len = 4;
        if (option_value(opts, opts_len, DHCP_OPTION_ROUTER, tmp, &tmp_len))
            gateway_ip = be32(tmp);

        tmp_len = 4;
        if (option_value(opts, opts_len, DHCP_OPTION_DNS, tmp, &tmp_len))
            dns_server_ip = be32(tmp);

        blockos::net::IPv4Address ip{{uint8_t(assigned_ip >> 24), uint8_t(assigned_ip >> 16), uint8_t(assigned_ip >> 8), uint8_t(assigned_ip)}};
        blockos::net::IPv4Address mask{{uint8_t(subnet_mask >> 24), uint8_t(subnet_mask >> 16), uint8_t(subnet_mask >> 8), uint8_t(subnet_mask)}};
        blockos::net::IPv4Address gw{{uint8_t(gateway_ip >> 24), uint8_t(gateway_ip >> 16), uint8_t(gateway_ip >> 8), uint8_t(gateway_ip)}};
        blockos::net::set_ipv4(ip, mask, gw);
        dhcp_success = true;
        state = 3;
    }
}

void DhcpDnsEngine::poll()
{
    blockos::net::poll();
}

bool DhcpDnsEngine::configure(uint32_t timeout_ticks)
{
    if (!blockos::net::is_initialized())
        return false;

    active = this;
    dhcp_success = false;
    state = 0;
    transaction_id += 0x10203041u;
    if (!blockos::net::udp_bind(DHCP_CLIENT_PORT, &DhcpDnsEngine::udp_callback))
        return false;
    state = 1;
    send_dhcp_discover();

    for (uint32_t i = 0; i < timeout_ticks; ++i) {
        poll();
        if (dhcp_success)
            return true;
        if ((i & 0x3FFu) == 0)
            __asm__ volatile("pause");
    }
    return false;
}

DhcpDnsEngine dynamic_net_stack;
