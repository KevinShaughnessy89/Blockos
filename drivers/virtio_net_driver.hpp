#pragma once

#include <stdint.h>

namespace virtio_net {

bool init();
bool is_available();

bool send_packet(
    const void* data,
    unsigned len
);

int receive_packet(
    void* buf,
    unsigned buf_len
);

void reclaim_tx();

// ------------------------------------------------------------
// MAC address
// ------------------------------------------------------------

bool get_mac_address(
    uint8_t out_mac[6]
);

}
