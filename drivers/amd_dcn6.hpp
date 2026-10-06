#pragma once

#include <stdint.h>
#include <stddef.h>

namespace amd_dcn6 {

enum class Status : int {
    Ok                 = 0,
    InvalidArgument    = -1,
    NotFound           = -2,
    NotSupported       = -3,
    AlreadyInitialized = -4,
    HardwareError      = -5,
    Timeout            = -6,
    Busy               = -7,
    PermissionDenied   = -8,
    NotReady           = -9,
    Fault              = -10
};

enum class PixelFormat : uint32_t {
    XRGB8888 = 0,
    ARGB8888 = 1,
    RGB565 = 2,
    XRGB2101010 = 3,
};

enum class ConnectorType : uint32_t {
    Unknown = 0,
    DP = 1,
    HDMI = 2,
    EDP = 3
};

struct DisplayMode {
    uint32_t width{};
    uint32_t height{};
    uint32_t refresh_hz{};
    uint32_t htotal{};
    uint32_t hsync_start{};
    uint32_t hsync_end{};
    uint32_t vtotal{};
    uint32_t vsync_start{};
    uint32_t vsync_end{};
    uint32_t pixel_clock_khz{};
    bool interlaced{};
    bool hsync_positive{true};
    bool vsync_positive{true};
};

class RegisterIO {
public:
    virtual ~RegisterIO() = default;
    virtual uint32_t read32(uint32_t reg) = 0;
    virtual void write32(uint32_t reg, uint32_t value) = 0;
    virtual void write_mask(uint32_t reg, uint32_t mask, uint32_t value);
    virtual bool valid(uint32_t reg) const = 0;
};

class MmioRegisterIO final : public RegisterIO {
private:
    volatile uint8_t* mmio_base;
    size_t mmio_size;
public:
    MmioRegisterIO();
    bool init(uintptr_t base, size_t size);
    uint32_t read32(uint32_t reg) override;
    void write32(uint32_t reg, uint32_t value) override;
    bool valid(uint32_t reg) const override;
    uintptr_t base() const;
    size_t size() const;
};

/*
 * All hardware-specific offsets/masks are supplied by the ASIC layer.
 * A zero offset means "not implemented on this profile" for optional
 * features; required blocks are checked during initialize().
 */
struct RegisterMap {
    uint32_t dchub_control{};
    uint32_t dchub_size{};

    uint32_t hubp_surface_addr{};
    uint32_t hubp_surface_addr_high{};
    uint32_t hubp_surface_pitch{};

    uint32_t dpp_control{};
    uint32_t opp_control{};
    uint32_t opp_pixel_format{};
    uint32_t optc_control{};

    uint32_t optc_h_total{};
    uint32_t optc_h_sync_a{};
    uint32_t optc_h_sync_b{};
    uint32_t optc_v_total{};
    uint32_t optc_v_sync_a{};
    uint32_t optc_v_sync_b{};

    uint32_t optc_underflow_status{};
    uint32_t viewport_x{};
    uint32_t viewport_y{};

    uint32_t vblank_status{};
    uint32_t vblank_ack{};

    /* Optional cursor path. */
    uint32_t cursor_surface_addr{};
    uint32_t cursor_surface_addr_high{};
    uint32_t cursor_control{};
    uint32_t cursor_position{};
    uint32_t cursor_size{};

    /* Optional hotplug / fault path. */
    uint32_t hotplug_status{};
    uint32_t fault_status{};
    uint32_t fault_clear{};

    /* Per-block control masks. */
    uint32_t dchub_enable_mask{};
    uint32_t dpp_enable_mask{};
    uint32_t opp_enable_mask{};
    uint32_t optc_enable_mask{1};

    /* Optional per-pipe register maps. */
    uint32_t pipe_stride{};
    uint32_t pipe_count{1};
};

struct Capabilities {
    uint32_t pipes{1};
    uint32_t streams{1};
    uint32_t max_width{8192};
    uint32_t max_height{8192};
    uint32_t max_pixel_clock_khz{1000000};

    bool supports_dp{};
    bool supports_hdmi{};
    bool supports_edp{};
    bool supports_page_flip{};
    bool supports_vblank{};
    bool supports_atomic_update{};
    bool supports_cursor{};
    bool supports_hotplug{};
    bool supports_fault_reporting{};
};

struct PlaneConfig {
    uint64_t framebuffer{};
    uint32_t width{};
    uint32_t height{};
    uint32_t pitch{};              /* bytes per scanline */
    PixelFormat format{PixelFormat::XRGB8888};
    uint32_t x{};
    uint32_t y{};
    uint32_t output_width{};
    uint32_t output_height{};
};

struct CursorConfig {
    uint64_t framebuffer{};
    uint32_t width{};
    uint32_t height{};
    uint32_t x{};
    uint32_t y{};
    bool enabled{};
};

struct Output {
    uint32_t id{};
    ConnectorType type{ConnectorType::Unknown};
    uint32_t pipe{};
    bool connected{};
    bool enabled{};
    DisplayMode mode{};
};

class Controller {
private:
    RegisterIO* regs;
    RegisterMap map;
    Capabilities caps;
    Output outputs[8];
    bool initialized;
    bool running;
    uint32_t output_count;

    uint32_t pipe_reg(uint32_t reg, uint32_t pipe) const;
    uint32_t bytes_per_pixel(PixelFormat format) const;
    bool check_mmio() const;
    bool validate_mode(const DisplayMode& mode) const;
    bool validate_plane(const PlaneConfig& plane) const;
    Status wait_vblank(uint32_t pipe, uint32_t timeout);
    Status program_timing(uint32_t pipe, const DisplayMode& mode);
    Status program_plane(uint32_t pipe, const PlaneConfig& plane);
    Status enable_output(uint32_t output);

public:
    Controller();

    Status initialize(RegisterIO* io, const RegisterMap& register_map);
    void set_capabilities(const Capabilities& capabilities);
    const Capabilities& get_capabilities() const;

    Status add_output(uint32_t id, ConnectorType type, bool connected = true,
                      uint32_t pipe = 0);
    Status set_output_connected(uint32_t output, bool connected);
    bool is_output_connected(uint32_t output) const;

    Status set_mode(uint32_t output, const DisplayMode& mode);
    Status set_plane(uint32_t output, const PlaneConfig& plane);
    Status page_flip(uint32_t output, uint64_t framebuffer);

    Status set_cursor(uint32_t output, const CursorConfig& cursor);
    Status check_faults(uint32_t output, uint32_t* fault_flags);
    Status enable(uint32_t output);
    Status disable(uint32_t output);

    bool is_initialized() const;
    bool is_running() const;
};

struct PciIdentity {
    uint16_t vendor{};
    uint16_t device{};
    uint8_t revision{};
    uint8_t class_code{};
    uint8_t subclass{};

    /* Filled by PCI/ASIC discovery when available. */
    uint8_t dcn_ip_major{};
    uint8_t dcn_ip_minor{};
};

bool is_amd_display_device(const PciIdentity& pci);

/* Conservative: only true when the discovery layer explicitly reports DCN 6.x. */
bool is_dcn6_candidate(const PciIdentity& pci);

extern Controller dcn6;

} // namespace amd_dcn6
