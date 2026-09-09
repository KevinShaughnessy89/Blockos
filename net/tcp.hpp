#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net.hpp"

namespace blockos::net {

enum class TcpState : uint8_t {
    Closed, Listen, SynSent, SynReceived, Established, FinWait1,
    FinWait2, CloseWait, LastAck, Closing, TimeWait
};

bool tcp_listen(uint16_t port);
int tcp_connect(const IPv4Address& dst, uint16_t port);
int tcp_send(int handle, const void* data, size_t len);
int tcp_receive(int handle, void* data, size_t len);
int tcp_close(int handle);

void tcp_receive(const IPv4Address& src, const void* packet, size_t len);
void tcp_tick();

} // namespace blockos::net
