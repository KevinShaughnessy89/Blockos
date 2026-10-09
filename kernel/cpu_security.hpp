#pragma once

#include <stdint.h>

namespace cpu_security {

/* Snapshot of the hardware features and controls observed on one CPU. */
struct Status {
    bool vendor_intel;
    bool vendor_amd;
    bool cpuid_leaf7;
    bool arch_caps_supported;

    bool ibrs_supported;
    bool ibpb_supported;
    bool stibp_supported;
    bool ssbd_supported;
    bool psfd_supported;
    bool ipred_control_supported;
    bool rrsba_control_supported;
    bool ddp_control_supported;
    bool bhi_control_supported;
    bool md_clear_supported;
    bool l1d_flush_supported;

    bool rdcl_no;
    bool mds_no;
    bool ssb_no;
    bool taa_no;
    bool bhi_no;
    bool tsx_control_supported;
    bool tsx_disabled;
    bool rngds_protection_enabled;
    bool gds_protection_enabled;
    bool kpti_recommended;

    bool ibrs_enabled;
    bool stibp_enabled;
    bool ssbd_enabled;
    bool psfd_enabled;
};

/* Call on every logical CPU during CPU bring-up, at CPL0. */
Status initialize();

/* Call after switching to a different address space (CR3). */
void on_context_switch();

} // namespace cpu_security

/* Assembly entry/exit hooks for legacy IBRS CPUs. */
extern "C" void blockos_cpu_security_enter_kernel();
extern "C" void blockos_cpu_security_leave_kernel();
