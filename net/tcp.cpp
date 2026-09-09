#include "tcp.hpp"
#include "ipv4.hpp"
#include <string.h>

namespace blockos::net {

struct TcpHeader {
    uint16_t src, dst;
    uint32_t seq, ack;
    uint8_t off_flags_hi, flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

static_assert(sizeof(TcpHeader) == 20);

constexpr uint8_t FIN=0x01, SYN=0x02, RST=0x04, ACK=0x10;

struct Conn {
    bool used;
    TcpState state;
    IPv4Address remote;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t rx[4096];
    size_t rx_head, rx_tail;
};

static Conn conns[16]{};
static uint16_t next_port = 49152;

static uint16_t tcp_checksum(const IPv4Address& src, const IPv4Address& dst,
                             const void* data, size_t len) {
    uint8_t pseudo[12]{};
    memcpy(pseudo, src.b, 4);
    memcpy(pseudo+4, dst.b, 4);
    pseudo[9] = IP_TCP;
    pseudo[10] = uint8_t(len >> 8);
    pseudo[11] = uint8_t(len);
    uint32_t sum = 0;
    const uint8_t* p = pseudo;
    for (size_t i=0;i<12;i+=2) sum += (p[i]<<8)|p[i+1];
    p = static_cast<const uint8_t*>(data);
    while (len >= 2) { sum += (p[0]<<8)|p[1]; p+=2; len-=2; }
    if (len) sum += p[0]<<8;
    while (sum>>16) sum=(sum&0xffff)+(sum>>16);
    return uint16_t(~sum);
}

static bool send_segment(Conn& c, uint8_t flags, const void* payload, size_t plen) {
    uint8_t packet[1480]{};
    if (plen + sizeof(TcpHeader) > sizeof(packet)) return false;
    auto* h = reinterpret_cast<TcpHeader*>(packet);
    h->src=htons(c.local_port); h->dst=htons(c.remote_port);
    h->seq=htonl(c.snd_nxt); h->ack=htonl(c.rcv_nxt);
    h->off_flags_hi=uint8_t(5<<4); h->flags=flags;
    h->window=htons(4096); h->checksum=0; h->urgent=0;
    memcpy(packet+sizeof(TcpHeader), payload, plen);
    auto src=ip_address();
    h->checksum=tcp_checksum(src,c.remote,packet,sizeof(TcpHeader)+plen);
    bool ok=ipv4_send(c.remote,IP_TCP,packet,sizeof(TcpHeader)+plen);
    if (ok) {
        if (flags&SYN) c.snd_nxt++;
        if (flags&FIN) c.snd_nxt++;
        c.snd_nxt += uint32_t(plen);
    }
    return ok;
}

bool tcp_listen(uint16_t port) {
    for (auto& c: conns) if (!c.used) {
        c={}; c.used=true; c.state=TcpState::Listen; c.local_port=port; return true;
    }
    return false;
}

int tcp_connect(const IPv4Address& dst, uint16_t port) {
    for (int i=0;i<16;i++) if (!conns[i].used) {
        Conn& c=conns[i]; c={}; c.used=true; c.state=TcpState::SynSent;
        c.remote=dst; c.local_port=next_port++; c.remote_port=port;
        c.snd_nxt=1; c.rcv_nxt=0;
        send_segment(c,SYN,nullptr,0);
        return i;
    }
    return -1;
}

void tcp_receive(const IPv4Address& src, const void* packet, size_t len) {
    if (len < sizeof(TcpHeader)) return;
    const auto* h=static_cast<const TcpHeader*>(packet);
    size_t hdr_len=((h->off_flags_hi>>4)&0xf)*4;
    if (hdr_len<20 || hdr_len>len) return;

    uint16_t dst=ntohs(h->dst), sport=ntohs(h->src);
    uint32_t seq=ntohl(h->seq), ack=ntohl(h->ack);
    uint8_t flags=h->flags;

    for (auto& c: conns) {
        if (!c.used || c.local_port!=dst) continue;

        if (c.state==TcpState::Listen && (flags&SYN)) {
            c.remote=src; c.remote_port=sport; c.state=TcpState::SynReceived;
            c.rcv_nxt=seq+1; c.snd_nxt=1;
            send_segment(c,SYN|ACK,nullptr,0);
            return;
        }

        if (c.remote!=src || c.remote_port!=sport) continue;

        if (c.state==TcpState::SynSent && (flags&(SYN|ACK))==(SYN|ACK)) {
            c.rcv_nxt=seq+1; c.snd_nxt=ack;
            c.state=TcpState::Established;
            send_segment(c,ACK,nullptr,0);
            return;
        }

        if (c.state==TcpState::SynReceived && (flags&ACK)) {
            c.snd_nxt=ack; c.state=TcpState::Established;
        }

        if (c.state==TcpState::Established) {
            const uint8_t* p=static_cast<const uint8_t*>(packet)+hdr_len;
            size_t plen=len-hdr_len;
            if (plen && seq==c.rcv_nxt) {
                for(size_t i=0;i<plen && c.rx_tail<sizeof(c.rx);++i)
                    c.rx[c.rx_tail++]=p[i];
                c.rcv_nxt+=uint32_t(plen);
                send_segment(c,ACK,nullptr,0);
            }
            if (flags&FIN) {
                c.rcv_nxt++;
                c.state=TcpState::CloseWait;
                send_segment(c,ACK,nullptr,0);
            }
        }
        return;
    }
}

int tcp_send(int handle,const void* data,size_t len) {
    if(handle<0||handle>=16||!conns[handle].used||
       conns[handle].state!=TcpState::Established) return -1;
    if(len>1400) len=1400;
    return send_segment(conns[handle],ACK,data,len)?int(len):-1;
}

int tcp_receive(int handle,void* data,size_t len) {
    if(handle<0||handle>=16||!conns[handle].used||!data) return -1;
    Conn& c=conns[handle];
    size_t avail=c.rx_tail-c.rx_head;
    if(!avail) return 0;
    if(len>avail) len=avail;
    memcpy(data,c.rx+c.rx_head,len); c.rx_head+=len;
    if(c.rx_head==c.rx_tail) c.rx_head=c.rx_tail=0;
    return int(len);
}

int tcp_close(int handle) {
    if(handle<0||handle>=16||!conns[handle].used) return -1;
    Conn& c=conns[handle];
    if(c.state==TcpState::Established) {
        if(!send_segment(c,FIN|ACK,nullptr,0)) return -1;
        c.state=TcpState::FinWait1;
    }
    return 0;
}

void tcp_tick() {}

} // namespace blockos::net
