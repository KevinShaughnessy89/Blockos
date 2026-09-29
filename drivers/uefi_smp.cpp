#include "uefi_smp.hpp"
#include <stdint.h>
#include <stddef.h>

extern "C" {
#include <efilib.h>
}

namespace {

static constexpr size_t MAX_CPUS = 64;
static constexpr size_t AP_STACK_SIZE = 16384;

static constexpr UINT32 EVT_NOTIFY_SIGNAL_VALUE = 0x00000200u;
static constexpr UINTN TPL_NOTIFY_VALUE = 16;

static EFI_SYSTEM_TABLE* g_system_table = nullptr;
static volatile UINT32 g_started = 0;
static volatile UINT32 g_release = 0;
static UINTN g_cpu_count = 1;
static void* g_stacks[MAX_CPUS]{};
static void* g_startup_event = nullptr;
static bool g_active = false;

#pragma pack(push, 1)

struct BlockOSMpProtocol;

using GetNumberOfProcessorsFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    UINTN*,
    UINTN*
);

using GetProcessorInfoFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    UINTN,
    void*
);

using StartupAllApsFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    void (EFIAPI *Procedure)(void*),
    BOOLEAN SingleThread,
    EFI_EVENT WaitEvent,
    UINTN TimeoutInMicroseconds,
    void* ProcedureArgument,
    UINTN** FailedCpuList
);

using StartupThisApFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    void (EFIAPI *Procedure)(void*),
    UINTN ProcessorNumber,
    EFI_EVENT WaitEvent,
    UINTN TimeoutInMicroseconds,
    void* ProcedureArgument,
    BOOLEAN* Finished
);

using SwitchBspFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    UINTN,
    BOOLEAN
);

using EnableDisableApFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    UINTN,
    BOOLEAN,
    uint32_t*
);

using WhoAmIFn = EFI_STATUS (EFIAPI *)(
    BlockOSMpProtocol*,
    UINTN*
);

struct BlockOSMpProtocol {
    GetNumberOfProcessorsFn GetNumberOfProcessors;
    GetProcessorInfoFn GetProcessorInfo;
    StartupAllApsFn StartupAllAPs;
    StartupThisApFn StartupThisAP;
    SwitchBspFn SwitchBSP;
    EnableDisableApFn EnableDisableAP;
    WhoAmIFn WhoAmI;
};

#pragma pack(pop)

static BlockOSMpProtocol* g_mp = nullptr;

/*
 * EFI_MP_SERVICES_PROTOCOL_GUID
 *
 * This must be an EFI_GUID object, because
 * LocateProtocol() expects EFI_GUID*.
 */
static EFI_GUID MP_SERVICES_GUID = {
    0x3fdda605,
    0xa76e,
    0x4f46,
    {
        0xad,
        0x29,
        0x12,
        0xf4,
        0x53,
        0x1b,
        0x3d,
        0x08
    }
};

static void EFIAPI ap_proc(void*)
{
    UINTN who = 0;

    if (!g_mp || !g_mp->WhoAmI)
        return;

    if (EFI_ERROR(
            g_mp->WhoAmI(
                g_mp,
                &who
            ))) {
        return;
    }

    if (who >= MAX_CPUS || !g_stacks[who])
        return;

    /*
     * Switch from the temporary firmware-provided AP stack
     * to a BlockOS-owned stack before ExitBootServices().
     */
    const uintptr_t stack_top =
        reinterpret_cast<uintptr_t>(
            g_stacks[who]
        ) +
        AP_STACK_SIZE -
        32;

    __asm__ volatile(
        "mov %0, %%rsp"
        :
        : "r"(stack_top)
        : "memory"
    );

    __atomic_add_fetch(
        &g_started,
        1u,
        __ATOMIC_SEQ_CST
    );

    /*
     * Wait here until the BSP tells the APs that the
     * firmware phase is finished.
     */
    while (!__atomic_load_n(
        &g_release,
        __ATOMIC_ACQUIRE
    )) {
        __asm__ volatile("pause");
    }

    /*
     * The current scheduler/IDT is still BSP-oriented.
     * Do not enter shared interrupt/scheduler state yet.
     */
    for (;;) {
        __asm__ volatile(
            "cli; hlt"
        );
    }
}

} // namespace

namespace uefi_smp {

bool start_aps(EFI_SYSTEM_TABLE* system_table)
{
    if (!system_table)
        return false;

    if (!system_table->BootServices)
        return false;

    if (g_active)
        return true;

    g_started = 0;
    g_release = 0;
    g_cpu_count = 1;

    EFI_STATUS st =
        (EFI_STATUS)uefi_call_wrapper(
            (void*)system_table
                ->BootServices
                ->LocateProtocol,
            3,
            &MP_SERVICES_GUID,
            nullptr,
            (void**)&g_mp
        );

    if (EFI_ERROR(st) || !g_mp)
        return false;

    UINTN total = 1;
    UINTN enabled = 1;

    st =
        g_mp->GetNumberOfProcessors(
            g_mp,
            &total,
            &enabled
        );

    if (EFI_ERROR(st) || total == 0)
        return false;

    if (total > MAX_CPUS)
        total = MAX_CPUS;

    g_cpu_count = total;

    /*
     * Allocate one private stack per CPU.
     */
    for (UINTN i = 0; i < total; ++i) {

        void* stack = nullptr;

        st =
            (EFI_STATUS)uefi_call_wrapper(
                (void*)system_table
                    ->BootServices
                    ->AllocatePool,
                3,
                EfiLoaderData,
                AP_STACK_SIZE,
                &stack
            );

        if (EFI_ERROR(st) || !stack)
            return false;

        g_stacks[i] = stack;
    }

    /*
     * Event used by EFI MP Services.
     */
    st =
        (EFI_STATUS)uefi_call_wrapper(
            (void*)system_table
                ->BootServices
                ->CreateEvent,
            5,
            EVT_NOTIFY_SIGNAL_VALUE,
            TPL_NOTIFY_VALUE,
            nullptr,
            nullptr,
            reinterpret_cast<EFI_EVENT*>(
                &g_startup_event
            )
        );

    if (EFI_ERROR(st))
        return false;

    UINTN* failed = nullptr;

    st =
        g_mp->StartupAllAPs(
            g_mp,
            ap_proc,
            FALSE,
            reinterpret_cast<EFI_EVENT>(
                g_startup_event
            ),
            0,
            nullptr,
            &failed
        );

    if (EFI_ERROR(st))
        return false;

    /*
     * Give APs some time to enter their callback and
     * switch to their private BlockOS-owned stacks.
     */
    const UINTN target =
        enabled > 0
            ? enabled - 1
            : 0;

    for (
        UINTN i = 0;
        i < 1000000;
        ++i
    ) {
        const UINT32 started =
            __atomic_load_n(
                &g_started,
                __ATOMIC_ACQUIRE
            );

        if (started >= target)
            break;

        __asm__ volatile("pause");
    }

    g_system_table = system_table;

    const UINT32 started =
        __atomic_load_n(
            &g_started,
            __ATOMIC_ACQUIRE
        );

    /*
     * There is no point enabling the SMP layer if
     * no AP actually started.
     */
    g_active =
        enabled > 1 &&
        started != 0;

    return g_active;
}

void release_aps()
{
    if (!g_active)
        return;

    __atomic_store_n(
        &g_release,
        1u,
        __ATOMIC_RELEASE
    );
}

size_t cpu_count()
{
    return static_cast<size_t>(
        g_cpu_count
    );
}

size_t started_count()
{
    return static_cast<size_t>(
        __atomic_load_n(
            &g_started,
            __ATOMIC_ACQUIRE
        )
    );
}

bool active()
{
    return g_active;
}

} // namespace uefi_smp
