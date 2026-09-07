#pragma once

#include <cstddef>
#include <cstdint>

#include "../network_manager.hpp"
#include "../network_adapter.hpp"
#include "../network_types.hpp"

namespace blockos::network::linux20 {

// Boundary used by the Linux 2.0 networking port.  The imported protocol
// implementation must use this boundary instead of hardware-specific I/O.
class BlockOSNetworkAdapter final {
public:
    explicit BlockOSNetworkAdapter(NetworkManager& manager) noexcept
        : manager_(manager) {}

    bool transmit(const void* frame, std::size_t length) noexcept {
        return frame != nullptr && length != 0 && manager_.send_raw(frame, length);
    }

    std::size_t receive(void* frame, std::size_t capacity) noexcept {
        if (frame == nullptr || capacity == 0) return 0;
        return manager_.recv_raw(frame, capacity);
    }

    void tick() noexcept { manager_.tick(); }

    NetworkManager& manager() noexcept { return manager_; }

private:
    NetworkManager& manager_;
};

} // namespace blockos::network::linux20
