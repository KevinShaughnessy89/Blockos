#include "tcp.hpp"
#include "ipv4.hpp"

#include <string.h>

namespace blockos::net {

namespace {

constexpr int MAX_CONNECTIONS = 16;

constexpr size_t RX_BUFFER_SIZE = 8192;
constexpr size_t TX_BUFFER_SIZE = 1460;

constexpr uint16_t EPHEMERAL_PORT_FIRST = 49152;
constexpr uint16_t EPHEMERAL_PORT_LAST  = 65535;

constexpr uint8_t FIN = 0x01;
constexpr uint8_t SYN = 0x02;
constexpr uint8_t RST = 0x04;
constexpr uint8_t PSH = 0x08;
constexpr uint8_t ACK = 0x10;

constexpr uint32_t INITIAL_SEQUENCE = 1;

constexpr uint32_t INITIAL_RTO = 30;
constexpr uint32_t MAX_RTO = 480;

constexpr uint8_t MAX_RETRIES = 6;

constexpr uint32_t TIME_WAIT_TICKS = 120;

constexpr uint16_t INITIAL_WINDOW = 4096;

constexpr uint32_t INITIAL_CWND = 1;
constexpr uint32_t INITIAL_SSTHRESH = 8;

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

struct TxSegment {
    bool used;
    uint32_t seq;
    uint32_t end_seq;

    uint8_t flags;

    size_t payload_len;
    uint8_t payload[TX_BUFFER_SIZE];

    uint32_t sent_tick;
    uint32_t retransmit_count;
};

struct OutOfOrderSegment {
    bool used;
    uint32_t seq;
    size_t len;
    uint8_t data[TX_BUFFER_SIZE];
};

struct Conn {
    bool used;

    TcpState state;

    IPv4Address remote;

    uint16_t local_port;
    uint16_t remote_port;

    uint32_t snd_una;
    uint32_t snd_nxt;

    uint32_t rcv_nxt;

    uint16_t advertised_window;

    uint32_t cwnd;
    uint32_t ssthresh;

    uint32_t rto;

    uint32_t last_ack;
    uint8_t duplicate_acks;

    uint32_t timer_tick;

    uint8_t retries;

    bool fin_sent;
    bool fin_received;

    uint8_t rx[RX_BUFFER_SIZE];
    size_t rx_head;
    size_t rx_tail;

    TxSegment tx;

    OutOfOrderSegment ooo;
};

Conn conns[MAX_CONNECTIONS]{};

uint16_t next_ephemeral_port = EPHEMERAL_PORT_FIRST;

uint32_t current_tick = 0;

bool sequence_less(uint32_t a, uint32_t b) {
    return int32_t(a - b) < 0;
}

bool sequence_less_equal(uint32_t a, uint32_t b) {
    return int32_t(a - b) <= 0;
}

bool sequence_greater(uint32_t a, uint32_t b) {
    return int32_t(a - b) > 0;
}

bool sequence_between(uint32_t x,
                      uint32_t start,
                      uint32_t end) {
    return !sequence_less(x, start) &&
           !sequence_greater(x, end);
}

uint16_t allocate_ephemeral_port() {
    for (uint32_t i = 0;
         i <= uint32_t(EPHEMERAL_PORT_LAST - EPHEMERAL_PORT_FIRST);
         ++i) {

        uint16_t candidate = next_ephemeral_port;

        ++next_ephemeral_port;

        if (next_ephemeral_port > EPHEMERAL_PORT_LAST)
            next_ephemeral_port = EPHEMERAL_PORT_FIRST;

        bool used = false;

        for (const auto& c : conns) {
            if (c.used && c.local_port == candidate) {
                used = true;
                break;
            }
        }

        if (!used)
            return candidate;
    }

    return 0;
}

uint16_t tcp_checksum(const IPv4Address& src,
                      const IPv4Address& dst,
                      const void* data,
                      size_t len) {
    uint32_t sum = 0;

    const uint8_t* s = src.b;
    const uint8_t* d = dst.b;

    sum += (uint32_t(s[0]) << 8) | s[1];
    sum += (uint32_t(s[2]) << 8) | s[3];

    sum += (uint32_t(d[0]) << 8) | d[1];
    sum += (uint32_t(d[2]) << 8) | d[3];

    sum += IP_TCP;

    sum += uint32_t(len);

    const uint8_t* p = static_cast<const uint8_t*>(data);

    while (len >= 2) {
        sum += (uint32_t(p[0]) << 8) | p[1];

        p += 2;
        len -= 2;

        while (sum >> 16)
            sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    if (len) {
        sum += uint32_t(p[0]) << 8;

        while (sum >> 16)
            sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);

    return uint16_t(~sum);
}

void clear_connection(Conn& c) {
    c = {};
    c.state = TcpState::Closed;
}

void reset_tx(Conn& c) {
    c.tx = {};
}

size_t rx_available(const Conn& c) {
    if (c.rx_tail < c.rx_head)
        return 0;

    return c.rx_tail - c.rx_head;
}

size_t rx_free_space(const Conn& c) {
    size_t used = rx_available(c);

    if (used >= RX_BUFFER_SIZE)
        return 0;

    return RX_BUFFER_SIZE - used;
}

void rx_reset_if_empty(Conn& c) {
    if (c.rx_head == c.rx_tail) {
        c.rx_head = 0;
        c.rx_tail = 0;
    }
}

bool rx_push(Conn& c,
             const uint8_t* data,
             size_t len) {
    if (!data || !len)
        return true;

    if (rx_free_space(c) < len)
        return false;

    memcpy(c.rx + c.rx_tail, data, len);

    c.rx_tail += len;

    return true;
}

bool send_raw_segment(Conn& c,
                      uint8_t flags,
                      uint32_t seq,
                      uint32_t ack,
                      const void* payload,
                      size_t payload_len) {

    uint8_t packet[1500]{};

    if (payload_len > TX_BUFFER_SIZE)
        return false;

    if (sizeof(TcpHeader) + payload_len > sizeof(packet))
        return false;

    TcpHeader* h =
        reinterpret_cast<TcpHeader*>(packet);

    h->src = htons(c.local_port);
    h->dst = htons(c.remote_port);

    h->seq = htonl(seq);
    h->ack = htonl(ack);

    h->off_flags_hi = uint8_t(5u << 4);
    h->flags = flags;

    uint32_t wnd =
        INITIAL_WINDOW;

    size_t free_space =
        rx_free_space(c);

    if (free_space < wnd)
        wnd = uint32_t(free_space);

    h->window = htons(uint16_t(wnd));

    h->checksum = 0;
    h->urgent = 0;

    if (payload && payload_len) {
        memcpy(packet + sizeof(TcpHeader),
               payload,
               payload_len);
    }

    IPv4Address local_ip = ip_address();

    h->checksum =
        tcp_checksum(local_ip,
                     c.remote,
                     packet,
                     sizeof(TcpHeader) + payload_len);

    return ipv4_send(c.remote,
                     IP_TCP,
                     packet,
                     sizeof(TcpHeader) + payload_len);
}

bool send_ack(Conn& c) {
    return send_raw_segment(c,
                            ACK,
                            c.snd_nxt,
                            c.rcv_nxt,
                            nullptr,
                            0);
}

bool send_rst(const IPv4Address& dst,
              uint16_t src_port,
              uint16_t dst_port,
              uint32_t seq,
              uint32_t ack,
              bool ack_flag) {

    uint8_t packet[sizeof(TcpHeader)]{};

    TcpHeader* h =
        reinterpret_cast<TcpHeader*>(packet);

    h->src = htons(src_port);
    h->dst = htons(dst_port);

    h->seq = htonl(seq);
    h->ack = htonl(ack);

    h->off_flags_hi = uint8_t(5u << 4);
    h->flags = uint8_t(RST | (ack_flag ? ACK : 0));

    h->window = 0;
    h->checksum = 0;
    h->urgent = 0;

    IPv4Address src = ip_address();

    h->checksum =
        tcp_checksum(src,
                     dst,
                     packet,
                     sizeof(packet));

    return ipv4_send(dst,
                     IP_TCP,
                     packet,
                     sizeof(packet));
}

bool send_tracked_segment(Conn& c,
                          uint8_t flags,
                          const void* payload,
                          size_t payload_len) {

    if (c.tx.used)
        return false;

    if (payload_len > TX_BUFFER_SIZE)
        return false;

    uint32_t seq = c.snd_nxt;

    if (!send_raw_segment(c,
                          flags,
                          seq,
                          c.rcv_nxt,
                          payload,
                          payload_len)) {
        return false;
    }

    c.tx.used = true;
    c.tx.seq = seq;
    c.tx.flags = flags;
    c.tx.payload_len = payload_len;
    c.tx.sent_tick = current_tick;
    c.tx.retransmit_count = 0;

    if (payload && payload_len) {
        memcpy(c.tx.payload,
               payload,
               payload_len);
    }

    uint32_t advance = uint32_t(payload_len);

    if (flags & SYN)
        ++advance;

    if (flags & FIN)
        ++advance;

    c.tx.end_seq = seq + advance;

    c.snd_nxt = c.tx.end_seq;

    c.timer_tick = current_tick;

    return true;
}

bool retransmit(Conn& c) {
    if (!c.tx.used)
        return false;

    if (c.tx.retransmit_count >= MAX_RETRIES) {
        clear_connection(c);
        return false;
    }

    ++c.tx.retransmit_count;

    uint32_t doubled = c.rto * 2;

    if (doubled > MAX_RTO)
        doubled = MAX_RTO;

    c.rto = doubled;

    c.ssthresh = c.cwnd / 2;

    if (c.ssthresh < 2)
        c.ssthresh = 2;

    c.cwnd = 1;

    bool ok =
        send_raw_segment(c,
                         c.tx.flags,
                         c.tx.seq,
                         c.rcv_nxt,
                         c.tx.payload,
                         c.tx.payload_len);

    if (!ok)
        return false;

    c.tx.sent_tick = current_tick;
    c.timer_tick = current_tick;

    return true;
}

void ack_segment(Conn& c,
                 uint32_t ack) {

    if (!c.tx.used)
        return;

    if (sequence_less(ack, c.tx.seq))
        return;

    if (sequence_greater(ack, c.tx.end_seq))
        return;

    if (ack == c.last_ack) {
        if (c.duplicate_acks < 255)
            ++c.duplicate_acks;

        if (c.duplicate_acks >= 3) {
            c.ssthresh = c.cwnd / 2;

            if (c.ssthresh < 2)
                c.ssthresh = 2;

            c.cwnd = c.ssthresh + 3;

            retransmit(c);

            c.duplicate_acks = 0;
        }

        return;
    }

    c.last_ack = ack;
    c.duplicate_acks = 0;

    c.tx.used = false;

    if (sequence_greater(ack, c.snd_una))
        c.snd_una = ack;

    if (c.cwnd < c.ssthresh) {
        ++c.cwnd;
    } else {
        if ((current_tick & 3u) == 0u)
            ++c.cwnd;
    }

    c.retries = 0;
    c.rto = INITIAL_RTO;

    if (c.state == TcpState::SynSent) {
        c.state = TcpState::Established;
    }

    if (c.state == TcpState::FinWait1) {
        if (c.fin_sent &&
            ack == c.snd_nxt) {

            if (c.fin_received)
                c.state = TcpState::TimeWait;
            else
                c.state = TcpState::FinWait2;
        }
    }

    if (c.state == TcpState::LastAck) {
        if (ack == c.snd_nxt) {
            clear_connection(c);
        }
    }
}

bool queue_out_of_order(Conn& c,
                        uint32_t seq,
                        const uint8_t* data,
                        size_t len) {

    if (!data || !len)
        return false;

    if (c.ooo.used)
        return false;

    if (len > TX_BUFFER_SIZE)
        return false;

    if (sequence_less(seq, c.rcv_nxt))
        return false;

    c.ooo.used = true;
    c.ooo.seq = seq;
    c.ooo.len = len;

    memcpy(c.ooo.data,
           data,
           len);

    return true;
}

void consume_out_of_order(Conn& c) {
    if (!c.ooo.used)
        return;

    if (c.ooo.seq != c.rcv_nxt)
        return;

    if (!rx_push(c,
                 c.ooo.data,
                 c.ooo.len)) {
        return;
    }

    c.rcv_nxt += uint32_t(c.ooo.len);

    c.ooo = {};
}

void enter_time_wait(Conn& c) {
    c.state = TcpState::TimeWait;
    c.timer_tick = current_tick;
    c.tx.used = false;
}

void process_fin(Conn& c,
                 uint32_t seq) {

    if (seq != c.rcv_nxt)
        return;

    ++c.rcv_nxt;

    c.fin_received = true;

    send_ack(c);

    switch (c.state) {

        case TcpState::Established:
            c.state = TcpState::CloseWait;
            break;

        case TcpState::FinWait1:
            if (!c.tx.used)
                enter_time_wait(c);
            else
                c.state = TcpState::Closing;
            break;

        case TcpState::FinWait2:
            enter_time_wait(c);
            break;

        case TcpState::Closing:
            enter_time_wait(c);
            break;

        default:
            break;
    }
}

} // namespace

bool tcp_init() {
    for (auto& c : conns)
        clear_connection(c);

    next_ephemeral_port = EPHEMERAL_PORT_FIRST;
    current_tick = 0;

    return true;
}

bool tcp_listen(uint16_t port) {
    if (!port)
        return false;

    for (auto& c : conns) {

        if (c.used &&
            c.state == TcpState::Listen &&
            c.local_port == port) {
            return false;
        }
    }

    for (auto& c : conns) {

        if (!c.used) {

            clear_connection(c);

            c.used = true;
            c.state = TcpState::Listen;
            c.local_port = port;
            c.advertised_window =
                INITIAL_WINDOW;

            return true;
        }
    }

    return false;
}

int tcp_connect(const IPv4Address& dst,
                uint16_t port) {

    if (!port)
        return -1;

    uint16_t local_port =
        allocate_ephemeral_port();

    if (!local_port)
        return -1;

    for (int i = 0;
         i < MAX_CONNECTIONS;
         ++i) {

        if (conns[i].used)
            continue;

        Conn& c = conns[i];

        clear_connection(c);

        c.used = true;
        c.state = TcpState::SynSent;

        c.remote = dst;

        c.local_port = local_port;
        c.remote_port = port;

        c.snd_una = INITIAL_SEQUENCE;
        c.snd_nxt = INITIAL_SEQUENCE;

        c.rcv_nxt = 0;

        c.cwnd = INITIAL_CWND;
        c.ssthresh = INITIAL_SSTHRESH;

        c.rto = INITIAL_RTO;

        c.last_ack = INITIAL_SEQUENCE;

        c.advertised_window =
            INITIAL_WINDOW;

        if (!send_tracked_segment(c,
                                  SYN,
                                  nullptr,
                                  0)) {
            clear_connection(c);
            return -1;
        }

        return i;
    }

    return -1;
}

void tcp_receive(const IPv4Address& src,
                 const void* packet,
                 size_t len) {

    if (!packet)
        return;

    if (len < sizeof(TcpHeader))
        return;

    const TcpHeader* h =
        static_cast<const TcpHeader*>(packet);

    size_t hdr_len =
        size_t((h->off_flags_hi >> 4) & 0x0F) * 4;

    if (hdr_len < sizeof(TcpHeader))
        return;

    if (hdr_len > len)
        return;

    if (tcp_checksum(ip_address(),
                     src,
                     packet,
                     len) != 0) {
        return;
    }

    uint16_t dst_port = ntohs(h->dst);
    uint16_t src_port = ntohs(h->src);

    uint32_t seq = ntohl(h->seq);
    uint32_t ack = ntohl(h->ack);

    uint16_t remote_window =
        ntohs(h->window);

    uint8_t flags = h->flags;

    const uint8_t* payload =
        static_cast<const uint8_t*>(packet) + hdr_len;

    size_t payload_len =
        len - hdr_len;

    if (flags & RST) {

        for (auto& c : conns) {

            if (!c.used)
                continue;

            if (c.local_port != dst_port)
                continue;

            if (c.state == TcpState::Listen)
                continue;

            if (c.remote != src)
                continue;

            if (c.remote_port != src_port)
                continue;

            clear_connection(c);
        }

        return;
    }

    Conn* cptr = nullptr;

    for (auto& c : conns) {

        if (!c.used)
            continue;

        if (c.local_port != dst_port)
            continue;

        if (c.state == TcpState::Listen) {

            if ((flags & SYN) &&
                !(flags & ACK)) {
                cptr = &c;
                break;
            }

            continue;
        }

        if (c.remote != src)
            continue;

        if (c.remote_port != src_port)
            continue;

        cptr = &c;
        break;
    }

    if (!cptr) {

        if (flags & SYN) {

            send_rst(src,
                     dst_port,
                     src_port,
                     0,
                     seq + 1,
                     true);

        } else {

            send_rst(src,
                     dst_port,
                     src_port,
                     ack,
                     seq + uint32_t(payload_len),
                     false);
        }

        return;
    }

    Conn& c = *cptr;

    if (c.state == TcpState::TimeWait) {

        if (flags & FIN)
            send_ack(c);

        c.timer_tick = current_tick;

        return;
    }

    c.advertised_window = remote_window;

    /*
     * LISTEN -> SYN-RECEIVED
     */
    if (c.state == TcpState::Listen) {

        if (!(flags & SYN))
            return;

        c.remote = src;
        c.remote_port = src_port;

        c.rcv_nxt = seq + 1;

        c.snd_una = INITIAL_SEQUENCE;
        c.snd_nxt = INITIAL_SEQUENCE;

        c.cwnd = INITIAL_CWND;
        c.ssthresh = INITIAL_SSTHRESH;

        c.rto = INITIAL_RTO;

        c.state = TcpState::SynReceived;

        reset_tx(c);

        send_tracked_segment(c,
                             SYN | ACK,
                             nullptr,
                             0);

        return;
    }

    /*
     * SYN-SENT
     */
    if (c.state == TcpState::SynSent) {

        if ((flags & SYN) &&
            (flags & ACK)) {

            if (c.tx.used &&
                ack != c.tx.end_seq) {
                return;
            }

            c.tx.used = false;

            c.snd_una = ack;
            c.rcv_nxt = seq + 1;

            c.last_ack = ack;

            c.cwnd = INITIAL_CWND;

            c.state = TcpState::Established;

            send_ack(c);

            return;
        }

        if (flags & SYN) {

            c.rcv_nxt = seq + 1;

            send_raw_segment(c,
                             SYN | ACK,
                             c.snd_nxt,
                             c.rcv_nxt,
                             nullptr,
                             0);

            c.state = TcpState::SynReceived;

            return;
        }

        return;
    }

    /*
     * SYN-RECEIVED
     */
    if (c.state == TcpState::SynReceived) {

        if (flags & ACK) {

            if (c.tx.used &&
                ack == c.tx.end_seq) {

                c.tx.used = false;
                c.snd_una = ack;
                c.state = TcpState::Established;
            }

            return;
        }

        return;
    }

    /*
     * ACK processing
     */
    if (flags & ACK) {

        if (sequence_less(ack, c.snd_una))
            return;

        if (sequence_greater(ack, c.snd_nxt))
            return;

        ack_segment(c, ack);
    }

    /*
     * Payload
     */
    if (payload_len) {

        if (seq == c.rcv_nxt) {

            if (rx_push(c,
                        payload,
                        payload_len)) {

                c.rcv_nxt +=
                    uint32_t(payload_len);

                consume_out_of_order(c);

                send_ack(c);
            } else {
                send_ack(c);
            }

        } else if (sequence_greater(seq,
                                    c.rcv_nxt)) {

            queue_out_of_order(c,
                               seq,
                               payload,
                               payload_len);

            send_ack(c);

        } else {
            /*
             * Duplicate/old data.
             */
            send_ack(c);
        }
    }

    /*
     * FIN processing
     */
    if (flags & FIN) {
        process_fin(c, seq + uint32_t(payload_len));
    }

    /*
     * FIN-WAIT-1
     */
    if (c.state == TcpState::FinWait1 &&
        !c.tx.used &&
        c.fin_sent) {

        if (c.fin_received)
            enter_time_wait(c);
        else
            c.state = TcpState::FinWait2;
    }

    /*
     * FIN-WAIT-2
     */
    if (c.state == TcpState::FinWait2 &&
        c.fin_received) {

        enter_time_wait(c);
    }

    /*
     * Closing
     */
    if (c.state == TcpState::Closing &&
        !c.tx.used) {

        enter_time_wait(c);
    }
}

int tcp_send(int handle,
             const void* data,
             size_t len) {

    if (handle < 0 ||
        handle >= MAX_CONNECTIONS)
        return -1;

    Conn& c = conns[handle];

    if (!c.used)
        return -1;

    if (c.state != TcpState::Established)
        return -1;

    if (!data && len)
        return -1;

    if (!len)
        return 0;

    /*
     * One tracked segment at a time in this implementation.
     * cwnd still controls growth/recovery, while retransmission
     * and duplicate-ACK handling remain active.
     */
    if (c.tx.used)
        return 0;

    if (len > TX_BUFFER_SIZE)
        len = TX_BUFFER_SIZE;

    size_t remote_window =
        c.advertised_window;

    if (remote_window == 0)
        return 0;

    if (len > remote_window)
        len = remote_window;

    if (c.cwnd == 0)
        return 0;

    if (!send_tracked_segment(c,
                              ACK | PSH,
                              data,
                              len)) {
        return -1;
    }

    return int(len);
}

int tcp_receive(int handle,
                void* data,
                size_t len) {

    if (handle < 0 ||
        handle >= MAX_CONNECTIONS)
        return -1;

    Conn& c = conns[handle];

    if (!c.used)
        return -1;

    if (!data && len)
        return -1;

    size_t available =
        rx_available(c);

    if (!available) {

        if (c.state == TcpState::CloseWait)
            return 0;

        return 0;
    }

    if (len > available)
        len = available;

    memcpy(data,
           c.rx + c.rx_head,
           len);

    c.rx_head += len;

    rx_reset_if_empty(c);

    return int(len);
}

int tcp_close(int handle) {

    if (handle < 0 ||
        handle >= MAX_CONNECTIONS)
        return -1;

    Conn& c = conns[handle];

    if (!c.used)
        return -1;

    switch (c.state) {

        case TcpState::Established: {

            if (c.tx.used)
                return 0;

            if (!send_tracked_segment(c,
                                      FIN | ACK,
                                      nullptr,
                                      0)) {
                return -1;
            }

            c.fin_sent = true;
            c.state = TcpState::FinWait1;

            return 0;
        }

        case TcpState::CloseWait: {

            if (c.tx.used)
                return 0;

            if (!send_tracked_segment(c,
                                      FIN | ACK,
                                      nullptr,
                                      0)) {
                return -1;
            }

            c.fin_sent = true;
            c.state = TcpState::LastAck;

            return 0;
        }

        case TcpState::Listen:
        case TcpState::SynSent:
        case TcpState::SynReceived:
            clear_connection(c);
            return 0;

        default:
            return 0;
    }
}

void tcp_tick() {

    ++current_tick;

    for (auto& c : conns) {

        if (!c.used)
            continue;

        /*
         * TIME-WAIT expiration.
         */
        if (c.state == TcpState::TimeWait) {

            if (current_tick -
                    c.timer_tick >=
                TIME_WAIT_TICKS) {

                clear_connection(c);
            }

            continue;
        }

        /*
         * Retransmission timer.
         */
        if (c.tx.used) {

            uint32_t elapsed =
                current_tick -
                c.tx.sent_tick;

            if (elapsed >= c.rto) {

                if (!retransmit(c))
                    continue;
            }
        }

        /*
         * SYN retransmission failure.
         */
        if ((c.state == TcpState::SynSent ||
             c.state == TcpState::SynReceived) &&
            c.tx.used) {

            if (c.tx.retransmit_count >= MAX_RETRIES) {
                clear_connection(c);
                continue;
            }
        }
    }
}

} // namespace blockos::net
