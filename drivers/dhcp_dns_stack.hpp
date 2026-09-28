#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../net/udp.hpp"

class DhcpDnsEngine {
public:
    DhcpDnsEngine();

    bool configure(uint32_t timeout_ticks = 3000000);
    void poll();
    bool ready() const { return dhcp_success; }

    uint32_t get_assigned_ip() const { return assigned_ip; }
    uint32_t get_netmask() const { return subnet_mask; }
    uint32_t get_gateway() const { return gateway_ip; }
    uint32_t get_dns() const { return dns_server_ip; }

    void send_dhcp_discover();

private:
    uint32_t transaction_id;
    uint32_t assigned_ip;
    uint32_t subnet_mask;
    uint32_t gateway_ip;
    uint32_t dns_server_ip;
    uint32_t server_ip;
    uint8_t client_mac[6];
    uint8_t state;
    bool dhcp_success;

    static void udp_callback(const blockos::net::UdpDatagram& datagram);
    void on_packet(const uint8_t* data, size_t len);
    void send_request(uint32_t requested_ip, uint32_t server);

    static DhcpDnsEngine* active;
};

extern DhcpDnsEngine dynamic_net_stack;
