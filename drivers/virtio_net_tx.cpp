#include "virtio_net_driver.hpp"

namespace virtio_net {

void force_reclaim_all()
{
    reclaim_tx();
}

} // namespace virtio_net
