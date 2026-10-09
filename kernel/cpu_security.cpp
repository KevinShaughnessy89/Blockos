#include "cpu_security.hpp"

#include <stdint.h>

namespace {

constexpr uint32_t IA32_SPEC_CTRL        = 0x00000048;
constexpr uint32_t IA32_PRED_CMD         = 0x00000049;
constexpr uint32_t IA32_ARCH_CAPABILITIES= 0x0000010A;
constexpr uint32_t IA32_FLUSH_CMD        = 0x0000010B;
constexpr uint32_t IA32_MCU_OPT_CTRL     = 0x00000123;
constexpr uint32_t IA32_TSX_CTRL         = 0x00000122;
constexpr uint32_t AMD64_VIRT_SPEC_CTRL  = 0xC001011F;

constexpr uint32_t CPUID7_EDX_MCU_OPT_CTRL = 1u << 9;
constexpr uint32_t CPUID7_EDX_MD_CLEAR     = 1u << 10;
constexpr uint32_t CPUID7_EDX_IBRS_IBPB    = 1u << 26;
constexpr uint32_t CPUID7_EDX_STIBP        = 1u << 27;
constexpr uint32_t CPUID7_EDX_L1D_FLUSH    = 1u << 28;
constexpr uint32_t CPUID7_EDX_ARCH_CAPS    = 1u << 29;
constexpr uint32_t CPUID7_EDX_SSBD         = 1u << 31;

constexpr uint32_t CPUID7_2_EDX_PSFD       = 1u << 0;
constexpr uint32_t CPUID7_2_EDX_IPRED_CTRL = 1u << 1;
constexpr uint32_t CPUID7_2_EDX_RRSBA_CTRL = 1u << 2;
constexpr uint32_t CPUID7_2_EDX_DDP_CTRL   = 1u << 3;
constexpr uint32_t CPUID7_2_EDX_BHI_CTRL   = 1u << 4;

constexpr uint32_t AMD_EXT8_EBX_IBPB       = 1u << 12;
constexpr uint32_t AMD_EXT8_EBX_IBRS       = 1u << 14;
constexpr uint32_t AMD_EXT8_EBX_STIBP      = 1u << 15;
constexpr uint32_t AMD_EXT8_EBX_IBRS_ALWAYS = 1u << 16;
constexpr uint32_t AMD_EXT8_EBX_IBRS_SAME  = 1u << 19;
constexpr uint32_t AMD_EXT8_EBX_SSBD       = 1u << 24;
constexpr uint32_t AMD_EXT8_EBX_VIRT_SSBD  = 1u << 25;
constexpr uint32_t AMD_EXT8_EBX_SSBD_NO    = 1u << 26;
constexpr uint32_t AMD_EXT8_EBX_PSFD       = 1u << 28;

constexpr uint64_t SPEC_CTRL_IBRS       = 1ull << 0;
constexpr uint64_t SPEC_CTRL_STIBP      = 1ull << 1;
constexpr uint64_t SPEC_CTRL_SSBD       = 1ull << 2;
constexpr uint64_t SPEC_CTRL_IPRED_DIS_U= 1ull << 3;
constexpr uint64_t SPEC_CTRL_IPRED_DIS_S= 1ull << 4;
constexpr uint64_t SPEC_CTRL_RRSBA_DIS_U= 1ull << 5;
constexpr uint64_t SPEC_CTRL_RRSBA_DIS_S= 1ull << 6;
constexpr uint64_t SPEC_CTRL_PSFD       = 1ull << 7;
constexpr uint64_t SPEC_CTRL_DDPD_U     = 1ull << 8;
constexpr uint64_t SPEC_CTRL_BHI_DIS_S  = 1ull << 10;

constexpr uint64_t ARCH_CAP_RDCL_NO      = 1ull << 0;
constexpr uint64_t ARCH_CAP_IBRS_ALL     = 1ull << 1;
constexpr uint64_t ARCH_CAP_SSB_NO       = 1ull << 4;
constexpr uint64_t ARCH_CAP_MDS_NO       = 1ull << 5;
constexpr uint64_t ARCH_CAP_TSX_CTRL     = 1ull << 7;
constexpr uint64_t ARCH_CAP_TAA_NO       = 1ull << 8;
constexpr uint64_t ARCH_CAP_GDS_CTRL     = 1ull << 25;

constexpr uint64_t MCU_OPT_RNGDS_MITG_DIS = 1ull << 0;
constexpr uint64_t MCU_OPT_GDS_MITG_DIS   = 1ull << 4;
constexpr uint64_t MCU_OPT_GDS_MITG_LOCK  = 1ull << 5;

struct CpuidRegs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static inline CpuidRegs cpuid(uint32_t leaf, uint32_t subleaf = 0) {
    CpuidRegs r{};
    __asm__ volatile("cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "a"(leaf), "c"(subleaf)
        : "memory");
    return r;
}

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo = 0, hi = 0;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    const uint32_t lo = static_cast<uint32_t>(value);
    const uint32_t hi = static_cast<uint32_t>(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static inline bool has_intel_vendor(const CpuidRegs& r) {
    return r.ebx == 0x756e6547u && r.edx == 0x49656e69u && r.ecx == 0x6c65746eu;
}

static inline bool has_amd_vendor(const CpuidRegs& r) {
    return r.ebx == 0x68747541u && r.edx == 0x69746e65u && r.ecx == 0x444d4163u;
}

/* True only when ordinary IBRS must be toggled across CPL3/CPL0 transitions. */
static bool legacy_ibrs_transition_required() {
    const CpuidRegs basic = cpuid(0);
    if (has_intel_vendor(basic)) {
        if (basic.eax < 7) return false;
        const CpuidRegs f = cpuid(7, 0);
        if (!(f.edx & CPUID7_EDX_IBRS_IBPB)) return false;
        if (!(f.edx & CPUID7_EDX_ARCH_CAPS)) return true;
        return (read_msr(IA32_ARCH_CAPABILITIES) & ARCH_CAP_IBRS_ALL) == 0;
    }

    if (has_amd_vendor(basic)) {
        const CpuidRegs extmax = cpuid(0x80000000u);
        if (extmax.eax < 0x80000008u) return false;
        const CpuidRegs f = cpuid(0x80000008u);
        if (!(f.ebx & AMD_EXT8_EBX_IBRS)) return false;
        return (f.ebx & (AMD_EXT8_EBX_IBRS_ALWAYS | AMD_EXT8_EBX_IBRS_SAME)) == 0;
    }
    return false;
}

static bool ibpb_supported_on_this_cpu() {
    const CpuidRegs basic = cpuid(0);
    if (has_intel_vendor(basic))
        return basic.eax >= 7 && (cpuid(7, 0).edx & CPUID7_EDX_IBRS_IBPB) != 0;
    if (has_amd_vendor(basic)) {
        const CpuidRegs extmax = cpuid(0x80000000u);
        return extmax.eax >= 0x80000008u &&
               (cpuid(0x80000008u).ebx & AMD_EXT8_EBX_IBPB) != 0;
    }
    return false;
}

} // namespace

namespace cpu_security {

Status initialize() {
    Status s{};
    const CpuidRegs basic = cpuid(0);
    const CpuidRegs extmax = cpuid(0x80000000u);
    s.vendor_intel = has_intel_vendor(basic);
    s.vendor_amd = has_amd_vendor(basic);

    if (s.vendor_intel) s.kpti_recommended = true;

    CpuidRegs f7{};
    CpuidRegs f72{};
    uint64_t arch_caps = 0;
    uint32_t amd_spec = 0;

    if (basic.eax >= 7) {
        f7 = cpuid(7, 0);
        s.cpuid_leaf7 = true;
        s.md_clear_supported = (f7.edx & CPUID7_EDX_MD_CLEAR) != 0;
    }

    if (s.vendor_intel && s.cpuid_leaf7) {
        s.ibrs_supported = (f7.edx & CPUID7_EDX_IBRS_IBPB) != 0;
        s.ibpb_supported = s.ibrs_supported;
        s.stibp_supported = (f7.edx & CPUID7_EDX_STIBP) != 0;
        s.ssbd_supported = (f7.edx & CPUID7_EDX_SSBD) != 0;
        s.l1d_flush_supported = (f7.edx & CPUID7_EDX_L1D_FLUSH) != 0;
        s.arch_caps_supported = (f7.edx & CPUID7_EDX_ARCH_CAPS) != 0;
        if (s.arch_caps_supported) {
            arch_caps = read_msr(IA32_ARCH_CAPABILITIES);
            s.rdcl_no = (arch_caps & ARCH_CAP_RDCL_NO) != 0;
            s.ssb_no = (arch_caps & ARCH_CAP_SSB_NO) != 0;
            s.mds_no = (arch_caps & ARCH_CAP_MDS_NO) != 0;
            s.taa_no = (arch_caps & ARCH_CAP_TAA_NO) != 0;
            s.tsx_control_supported = (arch_caps & ARCH_CAP_TSX_CTRL) != 0;
        }
        /* If the capability MSR is absent, do not assume Meltdown immunity. */
        s.kpti_recommended = !s.arch_caps_supported || !s.rdcl_no;
        if (f7.eax >= 2) {
            f72 = cpuid(7, 2);
            s.psfd_supported = (f72.edx & CPUID7_2_EDX_PSFD) != 0;
            s.ipred_control_supported = (f72.edx & CPUID7_2_EDX_IPRED_CTRL) != 0;
            s.rrsba_control_supported = (f72.edx & CPUID7_2_EDX_RRSBA_CTRL) != 0;
            s.ddp_control_supported = (f72.edx & CPUID7_2_EDX_DDP_CTRL) != 0;
            s.bhi_control_supported = (f72.edx & CPUID7_2_EDX_BHI_CTRL) != 0;
            if (s.arch_caps_supported)
                s.bhi_no = (arch_caps & (1ull << 20)) != 0;
        }
    } else if (s.vendor_amd && extmax.eax >= 0x80000008u) {
        amd_spec = cpuid(0x80000008u).ebx;
        s.ibrs_supported = (amd_spec & AMD_EXT8_EBX_IBRS) != 0;
        s.ibpb_supported = (amd_spec & AMD_EXT8_EBX_IBPB) != 0;
        s.stibp_supported = (amd_spec & AMD_EXT8_EBX_STIBP) != 0;
        s.ssbd_supported = (amd_spec & AMD_EXT8_EBX_SSBD) != 0;
        s.ssb_no = (amd_spec & AMD_EXT8_EBX_SSBD_NO) != 0;
        s.psfd_supported = (amd_spec & AMD_EXT8_EBX_PSFD) != 0;
    }

    /* Build only those IA32_SPEC_CTRL bits explicitly enumerated by this CPU. */
    uint64_t spec_mask = 0;
    if (s.ibrs_supported && !legacy_ibrs_transition_required())
        spec_mask |= SPEC_CTRL_IBRS;
    if (s.stibp_supported)
        spec_mask |= SPEC_CTRL_STIBP;
    if (s.ssbd_supported && !s.ssb_no) {
        if (s.vendor_amd && (amd_spec & AMD_EXT8_EBX_VIRT_SSBD)) {
            uint64_t virt = read_msr(AMD64_VIRT_SPEC_CTRL);
            if (!(virt & SPEC_CTRL_SSBD))
                write_msr(AMD64_VIRT_SPEC_CTRL, virt | SPEC_CTRL_SSBD);
            s.ssbd_enabled = true;
        } else {
            spec_mask |= SPEC_CTRL_SSBD;
        }
    }

    if (s.vendor_intel) {
        if (s.psfd_supported) spec_mask |= SPEC_CTRL_PSFD;
        if (s.ipred_control_supported)
            spec_mask |= SPEC_CTRL_IPRED_DIS_U | SPEC_CTRL_IPRED_DIS_S;
        if (s.rrsba_control_supported)
            spec_mask |= SPEC_CTRL_RRSBA_DIS_U | SPEC_CTRL_RRSBA_DIS_S;
        if (s.ddp_control_supported) spec_mask |= SPEC_CTRL_DDPD_U;
        if (s.bhi_control_supported && !s.bhi_no) spec_mask |= SPEC_CTRL_BHI_DIS_S;
    } else if (s.vendor_amd && s.psfd_supported) {
        spec_mask |= SPEC_CTRL_PSFD;
    }

    const bool legacy_ibrs = s.ibrs_supported && legacy_ibrs_transition_required();
    const uint64_t old_spec = (s.ibrs_supported || s.stibp_supported ||
                               (s.ssbd_supported && !s.ssb_no) ||
                               s.psfd_supported || s.ipred_control_supported ||
                               s.rrsba_control_supported || s.ddp_control_supported ||
                               (s.bhi_control_supported && !s.bhi_no))
        ? read_msr(IA32_SPEC_CTRL) : 0;
    uint64_t new_spec = old_spec | spec_mask;
    /* Legacy IBRS must be off in CPL3 and re-enabled on each entry. */
    if (legacy_ibrs)
        new_spec &= ~SPEC_CTRL_IBRS;
    if (new_spec != old_spec)
        write_msr(IA32_SPEC_CTRL, new_spec);

    s.ibrs_enabled = (new_spec & SPEC_CTRL_IBRS) != 0;
    s.stibp_enabled = (new_spec & SPEC_CTRL_STIBP) != 0;
    if (!s.ssbd_enabled)
        s.ssbd_enabled = (new_spec & SPEC_CTRL_SSBD) != 0;
    s.psfd_enabled = (new_spec & SPEC_CTRL_PSFD) != 0;

    if (s.ibpb_supported)
        write_msr(IA32_PRED_CMD, 1ull);

    /* Keep microcode opt-out bits cleared; lock GDS mitigation when supported. */
    if (s.vendor_intel && s.cpuid_leaf7 &&
        ((f7.edx & CPUID7_EDX_MCU_OPT_CTRL) ||
         (s.arch_caps_supported && (arch_caps & ARCH_CAP_GDS_CTRL)))) {
        uint64_t opt = read_msr(IA32_MCU_OPT_CTRL);
        uint64_t next = opt;
        if (f7.edx & CPUID7_EDX_MCU_OPT_CTRL)
            next &= ~MCU_OPT_RNGDS_MITG_DIS;
        if (s.arch_caps_supported && (arch_caps & ARCH_CAP_GDS_CTRL) &&
            !(opt & MCU_OPT_GDS_MITG_LOCK)) {
            next &= ~MCU_OPT_GDS_MITG_DIS;
            next |= MCU_OPT_GDS_MITG_LOCK;
        }
        if (next != opt) write_msr(IA32_MCU_OPT_CTRL, next);
        s.rngds_protection_enabled =
            !(f7.edx & CPUID7_EDX_MCU_OPT_CTRL) ||
            !(next & MCU_OPT_RNGDS_MITG_DIS);
        s.gds_protection_enabled =
            !(s.arch_caps_supported && (arch_caps & ARCH_CAP_GDS_CTRL)) ||
            !(next & MCU_OPT_GDS_MITG_DIS);
    }

    /* Disable RTM/TSX when the CPU explicitly advertises this control MSR. */
    if (s.vendor_intel && s.tsx_control_supported) {
        uint64_t tsx = read_msr(IA32_TSX_CTRL);
        const uint64_t disabled = tsx | 0x3ull; /* RTM_DISABLE | TSX_CPUID_CLEAR */
        if (disabled != tsx) write_msr(IA32_TSX_CTRL, disabled);
        s.tsx_disabled = true;
    }

    return s;
}

void on_context_switch() {
    if (ibpb_supported_on_this_cpu())
        write_msr(IA32_PRED_CMD, 1ull);

    /* High-security policy: flush L1D on address-space switch if advertised. */
    const CpuidRegs basic = cpuid(0);
    if (has_intel_vendor(basic) && basic.eax >= 7 &&
        (cpuid(7, 0).edx & CPUID7_EDX_L1D_FLUSH))
        write_msr(IA32_FLUSH_CMD, 1ull);
}

} // namespace cpu_security

extern "C" void blockos_cpu_security_enter_kernel() {
    if (!legacy_ibrs_transition_required()) return;
    const uint64_t spec = read_msr(IA32_SPEC_CTRL);
    if (!(spec & SPEC_CTRL_IBRS))
        write_msr(IA32_SPEC_CTRL, spec | SPEC_CTRL_IBRS);
}

extern "C" void blockos_cpu_security_leave_kernel() {
    if (!legacy_ibrs_transition_required()) return;
    const uint64_t spec = read_msr(IA32_SPEC_CTRL);
    if (spec & SPEC_CTRL_IBRS)
        write_msr(IA32_SPEC_CTRL, spec & ~SPEC_CTRL_IBRS);
}
