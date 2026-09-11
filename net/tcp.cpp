#include "tcp.hpp"
#include "ipv4.hpp"

#include <string.h>

namespace blockos::net {

/*
 * TCP header.
 *
 * The header is stored in network byte order when placed
 * into an outgoing packet.
 */
struct TcpHeader {
    uint16_t src;
    uint16_t dst;

    uint32_t seq;
    uint32_t ack;

    uint8_t off_flags_hi;
    uint8_t flags;

    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

static_assert(sizeof(TcpHeader) == 20);

/* TCP flags */
constexpr uint8_t FIN = 0x01;
constexpr uint8_t SYN = 0x02;
constexpr uint8_t RST = 0x04;
constexpr uint8_t ACK = 0x10;

/* Maximum number of simultaneously tracked TCP connections. */
constexpr size_t MAX_CONNECTIONS = 16;

/* TCP receive buffer size per connection. */
constexpr size_t TCP_RX_BUFFER_SIZE = 4096;

/* TCP advertised receive window. */
constexpr uint16_t TCP_WINDOW_SIZE = 4096;

/*
 * Connection state.
 */
struct Conn {
    bool used;

    TcpState state;

    IPv4Address remote;

    uint16_t local_port;
    uint16_t remote_port;

    uint32_t snd_nxt;
    uint32_t rcv_nxt;

    uint8_t rx[TCP_RX_BUFFER_SIZE];

    size_t rx_head;
    size_t rx_tail;
};

static Conn conns[MAX_CONNECTIONS]{};

/*
 * Ephemeral port range.
 */
static uint16_t next_port = 49152;

/*
 * Calculate the TCP checksum.
 *
 * TCP uses the IPv4 pseudo-header:
 *
 *   source address
 *   destination address
 *   zero
 *   protocol
 *   TCP length
 *
 * followed by the TCP header and payload.
 */
static uint16_t tcp_checksum(
    const IPv4Address& src,
    const IPv4Address& dst,
    const void* data,
    size_t len
) {
    uint8_t pseudo[12]{};

    memcpy(pseudo, src.b, 4);
    memcpy(pseudo + 4, dst.b, 4);

    pseudo[8] = 0;
    pseudo[9] = IP_TCP;

    pseudo[10] = uint8_t((len >> 8) & 0xff);
    pseudo[11] = uint8_t(len & 0xff);

    uint32_t sum = 0;

    const uint8_t* p = pseudo;

    for (size_t i = 0; i < sizeof(pseudo); i += 2) {
        sum += (uint16_t(p[i]) << 8) |
               uint16_t(p[i + 1]);
    }

    p = static_cast<const uint8_t*>(data);

    while (len >= 2) {
        sum += (uint16_t(p[0]) << 8) |
               uint16_t(p[1]);

        p += 2;
        len -= 2;
    }

    if (len != 0) {
        sum += uint16_t(p[0]) << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }

    return uint16_t(~sum);
}

/*
 * Send one TCP segment.
 */
static bool send_segment(
    Conn& c,
    uint8_t flags,
    const void* payload,
    size_t plen
) {
    uint8_t packet[1480]{};

    const size_t total_len =
        sizeof(TcpHeader) + plen;

    if (total_len > sizeof(packet)) {
        return false;
    }

    TcpHeader* h =
        reinterpret_cast<TcpHeader*>(packet);

    /*
     * TCP fields are transmitted in network byte order.
     */
    h->src = htons(c.local_port);
    h->dst = htons(c.remote_port);

    h->seq = htonl(c.snd_nxt);
    h->ack = htonl(c.rcv_nxt);

    /*
     * Data offset = 5 words = 20 bytes.
     */
    h->off_flags_hi = uint8_t(5u << 4);
    h->flags = flags;

    h->window = htons(TCP_WINDOW_SIZE);

    h->checksum = 0;
    h->urgent = 0;

    if (payload != nullptr && plen != 0) {
        memcpy(
            packet + sizeof(TcpHeader),
            payload,
            plen
        );
    }

    const IPv4Address src = ip_address();

    h->checksum =
        tcp_checksum(
            src,
            c.remote,
            packet,
            total_len
        );

    const bool ok =
        ipv4_send(
            c.remote,
            IP_TCP,
            packet,
            total_len
        );

    if (!ok) {
        return false;
    }

    /*
     * SYN and FIN each consume one sequence number.
     */
    if (flags & SYN) {
        c.snd_nxt++;
    }

    if (flags & FIN) {
        c.snd_nxt++;
    }

    /*
     * Payload consumes sequence space.
     */
    c.snd_nxt += uint32_t(plen);

    return true;
}

/*
 * Start listening on a TCP port.
 */
bool tcp_listen(uint16_t port) {
    for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
        Conn& c = conns[i];

        if (c.used) {
            continue;
        }

        c = {};

        c.used = true;
        c.state = TcpState::Listen;
        c.local_port = port;

        return true;
    }

    return false;
}

/*
 * Connect to a remote IPv4 TCP endpoint.
 */
int tcp_connect(
    const IPv4Address& dst,
    uint16_t port
) {
    for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
        Conn& c = conns[i];

        if (c.used) {
            continue;
        }

        c = {};

        c.used = true;
        c.state = TcpState::SynSent;

        c.remote = dst;

        c.local_port = next_port++;

        /*
         * Avoid wrapping into the reserved/low range.
         */
        if (next_port < 49152) {
            next_port = 49152;
        }

        c.remote_port = port;

        /*
         * Initial sequence number.
         *
         * A future version can replace this with a
         * stronger per-connection sequence generator.
         */
        c.snd_nxt = 1;
        c.rcv_nxt = 0;

        if (!send_segment(c, SYN, nullptr, 0)) {
            c = {};
            return -1;
        }

        return int(i);
    }

    return -1;
}

/*
 * Process an incoming TCP segment.
 */
void tcp_receive(
    const IPv4Address& src,
    const void* packet,
    size_t len
) {
    if (packet == nullptr) {
        return;
    }

    if (len < sizeof(TcpHeader)) {
        return;
    }

    const TcpHeader* h =
        static_cast<const TcpHeader*>(packet);

    /*
     * TCP data offset is stored in the upper
     * four bits of the first offset/flags byte.
     */
    const size_t hdr_len =
        size_t((h->off_flags_hi >> 4) & 0x0f) * 4;

    if (hdr_len < sizeof(TcpHeader)) {
        return;
    }

    if (hdr_len > len) {
        return;
    }

    const uint16_t dst =
        ntohs(h->dst);

    const uint16_t sport =
        ntohs(h->src);

    const uint32_t seq =
        ntohl(h->seq);

    const uint32_t ack =
        ntohl(h->ack);

    const uint8_t flags =
        h->flags;

    /*
     * Find a connection belonging to the
     * destination port.
     */
    for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
        Conn& c = conns[i];

        if (!c.used) {
            continue;
        }

        if (c.local_port != dst) {
            continue;
        }

        /*
         * Passive open:
         *
         * LISTEN + SYN
         *
         * -> SYN-ACK
         */
        if (c.state == TcpState::Listen &&
            (flags & SYN)) {

            c.remote = src;
            c.remote_port = sport;

            c.state = TcpState::SynReceived;

            c.rcv_nxt = seq + 1;
            c.snd_nxt = 1;

            send_segment(
                c,
                SYN | ACK,
                nullptr,
                0
            );

            return;
        }

        /*
         * Existing connection must match
         * both remote address and port.
         */
        if (c.remote != src) {
            continue;
        }

        if (c.remote_port != sport) {
            continue;
        }

        /*
         * Active connection:
         *
         * SYN-SENT + SYN/ACK
         *
         * -> ACK
         * -> ESTABLISHED
         */
        if (c.state == TcpState::SynSent &&
            (flags & (SYN | ACK)) == (SYN | ACK)) {

            c.rcv_nxt = seq + 1;

            /*
             * The peer ACK acknowledges our SYN.
             */
            c.snd_nxt = ack;

            c.state = TcpState::Established;

            send_segment(
                c,
                ACK,
                nullptr,
                0
            );

            return;
        }

        /*
         * Passive connection:
         *
         * SYN-RECEIVED + ACK
         *
         * -> ESTABLISHED
         */
        if (c.state == TcpState::SynReceived &&
            (flags & ACK)) {

            c.snd_nxt = ack;
            c.state = TcpState::Established;
        }

        /*
         * Established connection.
         */
        if (c.state == TcpState::Established) {

            const uint8_t* p =
                static_cast<const uint8_t*>(packet) +
                hdr_len;

            const size_t plen =
                len - hdr_len;

            /*
             * Only accept data that starts exactly
             * at the sequence number we expect.
             *
             * Out-of-order buffering can be added later.
             */
            if (plen != 0 &&
                seq == c.rcv_nxt) {

                size_t free_space =
                    sizeof(c.rx) - c.rx_tail;

                size_t copy_len = plen;

                if (copy_len > free_space) {
                    copy_len = free_space;
                }

                if (copy_len != 0) {
                    memcpy(
                        c.rx + c.rx_tail,
                        p,
                        copy_len
                    );

                    c.rx_tail += copy_len;

                    c.rcv_nxt +=
                        uint32_t(copy_len);
                }

                /*
                 * ACK everything that was accepted.
                 */
                send_segment(
                    c,
                    ACK,
                    nullptr,
                    0
                );
            }

            /*
             * FIN consumes one sequence number.
             */
            if (flags & FIN) {

                if (seq == c.rcv_nxt) {
                    c.rcv_nxt++;
                }

                c.state = TcpState::CloseWait;

                send_segment(
                    c,
                    ACK,
                    nullptr,
                    0
                );
            }
        }

        return;
    }
}

/*
 * Send application data.
 */
int tcp_send(
    int handle,
    const void* data,
    size_t len
) {
    if (handle < 0 ||
        handle >= int(MAX_CONNECTIONS)) {
        return -1;
    }

    if (data == nullptr && len != 0) {
        return -1;
    }

    Conn& c = conns[handle];

    if (!c.used ||
        c.state != TcpState::Established) {
        return -1;
    }

    /*
     * Keep the segment below the current
     * IPv4 packet buffer limit.
     */
    constexpr size_t MAX_TCP_PAYLOAD = 1400;

    if (len > MAX_TCP_PAYLOAD) {
        len = MAX_TCP_PAYLOAD;
    }

    if (!send_segment(c, ACK, data, len)) {
        return -1;
    }

    return int(len);
}

/*
 * Receive application data.
 */
int tcp_receive(
    int handle,
    void* data,
    size_t len
) {
    if (handle < 0 ||
        handle >= int(MAX_CONNECTIONS)) {
        return -1;
    }

    if (data == nullptr && len != 0) {
        return -1;
    }

    Conn& c = conns[handle];

    if (!c.used) {
        return -1;
    }

    const size_t avail =
        c.rx_tail - c.rx_head;

    if (avail == 0) {
        return 0;
    }

    if (len > avail) {
        len = avail;
    }

    if (len != 0) {
        memcpy(
            data,
            c.rx + c.rx_head,
            len
        );

        c.rx_head += len;
    }

    /*
     * Compact the buffer after everything
     * has been consumed.
     */
    if (c.rx_head == c.rx_tail) {
        c.rx_head = 0;
        c.rx_tail = 0;
    }

    return int(len);
}

/*
 * Close a TCP connection.
 */
int tcp_close(int handle) {
    if (handle < 0 ||
        handle >= int(MAX_CONNECTIONS)) {
        return -1;
    }

    Conn& c = conns[handle];

    if (!c.used) {
        return -1;
    }

    if (c.state == TcpState::Established) {

        if (!send_segment(
                c,
                FIN | ACK,
                nullptr,
                0)) {
            return -1;
        }

        c.state = TcpState::FinWait1;

        return 0;
    }

    if (c.state == TcpState::CloseWait) {

        if (!send_segment(
                c,
                FIN | ACK,
                nullptr,
                0)) {
            return -1;
        }

        c.state = TcpState::LastAck;

        return 0;
    }

    return 0;
}

/*
 * TCP timer processing.
 *
 * Currently intentionally lightweight.
 *
 * Future networking versions should add:
 *
 * - SYN retransmission
 * - TCP retransmission
 * - retransmission timeout
 * - duplicate ACK handling
 * - congestion control
 * - TIME_WAIT handling
 * - keepalive
 */
void tcp_tick() {
}

} // namespace blockos::net
