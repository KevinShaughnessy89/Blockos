#include "uefi_runtime.hpp"

namespace uefi_runtime {
namespace {
static Services runtime_services{};
static bool ready = false;
}

bool init(const Services& services_in) {
    runtime_services = services_in;
    ready = services_in.get_time != 0 ||
            services_in.get_variable != 0 ||
            services_in.reset_system != 0;
    return ready;
}

bool available() {
    return ready;
}

const Services& services() {
    return runtime_services;
}

} // namespace uefi_runtime
