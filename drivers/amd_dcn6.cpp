#include "amd_dcn6.hpp"

#include <stdint.h>
#include <stddef.h>

namespace amd_dcn6 {

void RegisterIO::write_mask(uint32_t reg, uint32_t mask, uint32_t value)
{
    const uint32_t old_value = read32(reg);
    write32(reg, (old_value & ~mask) | (value & mask));
}

MmioRegisterIO::MmioRegisterIO() : mmio_base(nullptr), mmio_size(0) {}

bool MmioRegisterIO::init(uintptr_t base, size_t size)
{
    if (base == 0 || size < sizeof(uint32_t))
        return false;
    mmio_base = reinterpret_cast<volatile uint8_t*>(base);
    mmio_size = size;
    return true;
}

uint32_t MmioRegisterIO::read32(uint32_t reg)
{
    if (!valid(reg))
        return 0;
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(mmio_base + reg);
    return *ptr;
}

void MmioRegisterIO::write32(uint32_t reg, uint32_t value)
{
    if (!valid(reg))
        return;
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(mmio_base + reg);
    *ptr = value;
}

bool MmioRegisterIO::valid(uint32_t reg) const
{
    if (!mmio_base || (reg & 3U) != 0)
        return false;
    return reg <= mmio_size && mmio_size - reg >= sizeof(uint32_t);
}

uintptr_t MmioRegisterIO::base() const
{
    return reinterpret_cast<uintptr_t>(mmio_base);
}

size_t MmioRegisterIO::size() const
{
    return mmio_size;
}

Controller::Controller()
    : regs(nullptr), map{}, caps{}, outputs{}, initialized(false), running(false), output_count(0) {}

uint32_t Controller::pipe_reg(uint32_t reg, uint32_t pipe) const
{
    if (reg == 0 || map.pipe_stride == 0 || pipe == 0)
        return reg;
    return reg + pipe * map.pipe_stride;
}

uint32_t Controller::bytes_per_pixel(PixelFormat format) const
{
    switch (format) {
        case PixelFormat::RGB565: return 2;
        case PixelFormat::XRGB8888:
        case PixelFormat::ARGB8888:
        case PixelFormat::XRGB2101010: return 4;
        default: return 0;
    }
}

bool Controller::check_mmio() const
{
    if (!regs)
        return false;
    if (!regs->valid(pipe_reg(map.dchub_control, 0)))
        return false;
    if (!regs->valid(pipe_reg(map.optc_control, 0)))
        return false;
    if (!regs->valid(pipe_reg(map.optc_h_total, 0)) ||
        !regs->valid(pipe_reg(map.optc_v_total, 0)))
        return false;
    return true;
}

bool Controller::validate_mode(const DisplayMode& mode) const
{
    if (!mode.width || !mode.height || !mode.refresh_hz)
        return false;
    if (mode.width > caps.max_width || mode.height > caps.max_height)
        return false;
    if (!mode.htotal || mode.htotal <= mode.width || mode.htotal > 0xFFFFu)
        return false;
    if (!mode.vtotal || mode.vtotal <= mode.height || mode.vtotal > 0xFFFFu)
        return false;
    if (mode.hsync_start < mode.width || mode.hsync_end <= mode.hsync_start ||
        mode.hsync_end > mode.htotal)
        return false;
    if (mode.vsync_start < mode.height || mode.vsync_end <= mode.vsync_start ||
        mode.vsync_end > mode.vtotal)
        return false;
    if (mode.pixel_clock_khz && mode.pixel_clock_khz > caps.max_pixel_clock_khz)
        return false;
    return true;
}

bool Controller::validate_plane(const PlaneConfig& plane) const
{
    const uint32_t bpp = bytes_per_pixel(plane.format);
    if (!plane.framebuffer || !plane.width || !plane.height || !plane.pitch || !bpp)
        return false;
    if (plane.pitch < plane.width * bpp || (plane.pitch & 3U) != 0)
        return false;
    if (!plane.output_width || !plane.output_height)
        return false;
    if (plane.width > caps.max_width || plane.height > caps.max_height)
        return false;
    return true;
}

Status Controller::initialize(RegisterIO* io, const RegisterMap& register_map)
{
    if (initialized)
        return Status::AlreadyInitialized;
    if (!io)
        return Status::InvalidArgument;

    regs = io;
    map = register_map;

    if (!map.pipe_count)
        map.pipe_count = 1;
    if (map.pipe_count > 8)
        map.pipe_count = 8;
    if (!map.pipe_count || !check_mmio()) {
        regs = nullptr;
        return Status::HardwareError;
    }

    if (caps.pipes == 0 || caps.pipes > map.pipe_count)
        caps.pipes = map.pipe_count;

    initialized = true;
    running = false;
    output_count = 0;
    return Status::Ok;
}

void Controller::set_capabilities(const Capabilities& capabilities)
{
    caps = capabilities;
    if (caps.pipes == 0)
        caps.pipes = 1;
    if (caps.pipes > 8)
        caps.pipes = 8;
}

const Capabilities& Controller::get_capabilities() const
{
    return caps;
}

Status Controller::add_output(uint32_t id, ConnectorType type, bool connected, uint32_t pipe)
{
    if (!initialized)
        return Status::HardwareError;
    if (output_count >= 8)
        return Status::Busy;
    if (type == ConnectorType::Unknown)
        return Status::InvalidArgument;
    if (pipe >= caps.pipes || pipe >= 8)
        return Status::InvalidArgument;
    if (type == ConnectorType::DP && !caps.supports_dp)
        return Status::NotSupported;
    if (type == ConnectorType::HDMI && !caps.supports_hdmi)
        return Status::NotSupported;
    if (type == ConnectorType::EDP && !caps.supports_edp)
        return Status::NotSupported;

    Output& out = outputs[output_count++];
    out.id = id;
    out.type = type;
    out.pipe = pipe;
    out.connected = connected;
    out.enabled = false;
    out.mode = {};
    return Status::Ok;
}

Status Controller::set_output_connected(uint32_t output, bool connected)
{
    if (!initialized)
        return Status::HardwareError;
    if (output >= output_count)
        return Status::NotFound;
    outputs[output].connected = connected;
    if (!connected && outputs[output].enabled)
        return disable(output);
    return Status::Ok;
}

bool Controller::is_output_connected(uint32_t output) const
{
    return initialized && output < output_count && outputs[output].connected;
}

Status Controller::program_timing(uint32_t pipe, const DisplayMode& mode)
{
    if (!regs || pipe >= caps.pipes || !validate_mode(mode))
        return Status::InvalidArgument;
    if (mode.interlaced)
        return Status::NotSupported;

    const uint32_t htotal = pipe_reg(map.optc_h_total, pipe);
    const uint32_t hsync_a = pipe_reg(map.optc_h_sync_a, pipe);
    const uint32_t hsync_b = pipe_reg(map.optc_h_sync_b, pipe);
    const uint32_t vtotal = pipe_reg(map.optc_v_total, pipe);
    const uint32_t vsync_a = pipe_reg(map.optc_v_sync_a, pipe);
    const uint32_t vsync_b = pipe_reg(map.optc_v_sync_b, pipe);

    if (!regs->valid(htotal) || !regs->valid(vtotal))
        return Status::HardwareError;

    regs->write32(htotal, mode.htotal);
    if (regs->valid(hsync_a))
        regs->write32(hsync_a, ((mode.hsync_start & 0xFFFFU) << 16) | (mode.hsync_end & 0xFFFFU));
    if (regs->valid(hsync_b))
        regs->write32(hsync_b, mode.hsync_positive ? 1U : 0U);

    regs->write32(vtotal, mode.vtotal);
    if (regs->valid(vsync_a))
        regs->write32(vsync_a, ((mode.vsync_start & 0xFFFFU) << 16) | (mode.vsync_end & 0xFFFFU));
    if (regs->valid(vsync_b))
        regs->write32(vsync_b, mode.vsync_positive ? 1U : 0U);
    return Status::Ok;
}

Status Controller::program_plane(uint32_t pipe, const PlaneConfig& plane)
{
    if (!regs || pipe >= caps.pipes || !validate_plane(plane))
        return Status::InvalidArgument;

    const uint32_t surface = pipe_reg(map.hubp_surface_addr, pipe);
    const uint32_t surface_hi = pipe_reg(map.hubp_surface_addr_high, pipe);
    const uint32_t pitch = pipe_reg(map.hubp_surface_pitch, pipe);
    const uint32_t viewport_x = pipe_reg(map.viewport_x, pipe);
    const uint32_t viewport_y = pipe_reg(map.viewport_y, pipe);
    const uint32_t format_reg = pipe_reg(map.opp_pixel_format, pipe);

    if (!regs->valid(surface) || !regs->valid(pitch))
        return Status::HardwareError;

    regs->write32(surface, static_cast<uint32_t>(plane.framebuffer));
    if (surface_hi && regs->valid(surface_hi))
        regs->write32(surface_hi, static_cast<uint32_t>(plane.framebuffer >> 32));

    regs->write32(pitch, plane.pitch);

    if (regs->valid(viewport_x))
        regs->write32(viewport_x, ((plane.x & 0xFFFFU) << 16) | (plane.y & 0xFFFFU));
    if (regs->valid(viewport_y))
        regs->write32(viewport_y,
                      ((plane.output_width & 0xFFFFU) << 16) |
                      (plane.output_height & 0xFFFFU));

    uint32_t format = 0;
    switch (plane.format) {
        case PixelFormat::XRGB8888: format = 0; break;
        case PixelFormat::ARGB8888: format = 1; break;
        case PixelFormat::RGB565: format = 2; break;
        case PixelFormat::XRGB2101010: format = 3; break;
        default: return Status::InvalidArgument;
    }
    if (regs->valid(format_reg))
        regs->write32(format_reg, format);

    return Status::Ok;
}

Status Controller::wait_vblank(uint32_t pipe, uint32_t timeout)
{
    if (!regs || !caps.supports_vblank || pipe >= caps.pipes)
        return Status::NotSupported;

    const uint32_t status_reg = pipe_reg(map.vblank_status, pipe);
    if (!status_reg || !regs->valid(status_reg))
        return Status::NotSupported;

    uint32_t previous = regs->read32(status_reg) & 1U;
    for (uint32_t i = 0; i < timeout; ++i) {
        const uint32_t now = regs->read32(status_reg) & 1U;
        if (now != previous || (now && !previous)) {
            if (map.vblank_ack && regs->valid(pipe_reg(map.vblank_ack, pipe)))
                regs->write32(pipe_reg(map.vblank_ack, pipe), 1U);
            return Status::Ok;
        }
        previous = now;
        asm volatile("pause");
    }
    return Status::Timeout;
}

Status Controller::enable_output(uint32_t output)
{
    if (output >= output_count)
        return Status::NotFound;
    Output& out = outputs[output];
    if (!out.connected)
        return Status::NotFound;

    const uint32_t pipe = out.pipe;
    const uint32_t dchub = pipe_reg(map.dchub_control, pipe);
    const uint32_t dpp = pipe_reg(map.dpp_control, pipe);
    const uint32_t opp = pipe_reg(map.opp_control, pipe);
    const uint32_t optc = pipe_reg(map.optc_control, pipe);

    if (map.dchub_enable_mask && regs->valid(dchub))
        regs->write_mask(dchub, map.dchub_enable_mask, map.dchub_enable_mask);
    if (map.dpp_enable_mask && regs->valid(dpp))
        regs->write_mask(dpp, map.dpp_enable_mask, map.dpp_enable_mask);
    if (map.opp_enable_mask && regs->valid(opp))
        regs->write_mask(opp, map.opp_enable_mask, map.opp_enable_mask);
    if (map.optc_enable_mask && regs->valid(optc))
        regs->write_mask(optc, map.optc_enable_mask, map.optc_enable_mask);

    out.enabled = true;
    running = true;
    return Status::Ok;
}

Status Controller::set_mode(uint32_t output, const DisplayMode& mode)
{
    if (!initialized)
        return Status::HardwareError;
    if (output >= output_count)
        return Status::NotFound;
    if (!outputs[output].connected)
        return Status::NotFound;

    const Status result = program_timing(outputs[output].pipe, mode);
    if (result != Status::Ok)
        return result;
    outputs[output].mode = mode;
    return Status::Ok;
}

Status Controller::set_plane(uint32_t output, const PlaneConfig& plane)
{
    if (!initialized)
        return Status::HardwareError;
    if (output >= output_count)
        return Status::NotFound;
    if (!outputs[output].connected)
        return Status::NotFound;
    return program_plane(outputs[output].pipe, plane);
}

Status Controller::page_flip(uint32_t output, uint64_t framebuffer)
{
    if (!initialized)
        return Status::HardwareError;
    if (!caps.supports_page_flip)
        return Status::NotSupported;
    if (!framebuffer || output >= output_count)
        return Status::InvalidArgument;
    if (!outputs[output].connected)
        return Status::NotFound;

    const uint32_t pipe = outputs[output].pipe;
    const Status sync = wait_vblank(pipe, 1000000);
    if (sync != Status::Ok)
        return sync;

    const uint32_t lo = pipe_reg(map.hubp_surface_addr, pipe);
    const uint32_t hi = pipe_reg(map.hubp_surface_addr_high, pipe);
    if (!regs->valid(lo))
        return Status::HardwareError;

    regs->write32(lo, static_cast<uint32_t>(framebuffer));
    if (hi && regs->valid(hi))
        regs->write32(hi, static_cast<uint32_t>(framebuffer >> 32));
    if (map.vblank_ack && regs->valid(pipe_reg(map.vblank_ack, pipe)))
        regs->write32(pipe_reg(map.vblank_ack, pipe), 1U);
    return Status::Ok;
}

Status Controller::set_cursor(uint32_t output, const CursorConfig& cursor)
{
    if (!initialized || output >= output_count)
        return Status::NotFound;
    if (!caps.supports_cursor)
        return Status::NotSupported;
    if (!cursor.framebuffer || !cursor.width || !cursor.height)
        return Status::InvalidArgument;

    const uint32_t pipe = outputs[output].pipe;
    const uint32_t addr = pipe_reg(map.cursor_surface_addr, pipe);
    const uint32_t addr_hi = pipe_reg(map.cursor_surface_addr_high, pipe);
    const uint32_t pos = pipe_reg(map.cursor_position, pipe);
    const uint32_t size = pipe_reg(map.cursor_size, pipe);
    const uint32_t control = pipe_reg(map.cursor_control, pipe);

    if (!regs->valid(addr) || !regs->valid(control))
        return Status::NotSupported;

    regs->write32(addr, static_cast<uint32_t>(cursor.framebuffer));
    if (addr_hi && regs->valid(addr_hi))
        regs->write32(addr_hi, static_cast<uint32_t>(cursor.framebuffer >> 32));
    if (regs->valid(pos))
        regs->write32(pos, ((cursor.x & 0xFFFFU) << 16) | (cursor.y & 0xFFFFU));
    if (regs->valid(size))
        regs->write32(size, ((cursor.width & 0xFFFFU) << 16) | (cursor.height & 0xFFFFU));
    regs->write_mask(control, 1U, cursor.enabled ? 1U : 0U);
    return Status::Ok;
}

Status Controller::check_faults(uint32_t output, uint32_t* fault_flags)
{
    if (!initialized || output >= output_count || !fault_flags)
        return Status::InvalidArgument;
    if (!caps.supports_fault_reporting)
        return Status::NotSupported;

    const uint32_t pipe = outputs[output].pipe;
    const uint32_t reg = pipe_reg(map.fault_status, pipe);
    if (!reg || !regs->valid(reg))
        return Status::NotSupported;

    *fault_flags = regs->read32(reg);
    if (*fault_flags && map.fault_clear) {
        const uint32_t clear = pipe_reg(map.fault_clear, pipe);
        if (regs->valid(clear))
            regs->write32(clear, *fault_flags);
    }
    return *fault_flags ? Status::Fault : Status::Ok;
}

Status Controller::enable(uint32_t output)
{
    if (!initialized)
        return Status::HardwareError;
    return enable_output(output);
}

Status Controller::disable(uint32_t output)
{
    if (!initialized)
        return Status::HardwareError;
    if (output >= output_count)
        return Status::NotFound;

    const uint32_t pipe = outputs[output].pipe;
    const uint32_t optc = pipe_reg(map.optc_control, pipe);
    if (map.optc_enable_mask && regs->valid(optc))
        regs->write_mask(optc, map.optc_enable_mask, 0);

    outputs[output].enabled = false;
    running = false;
    for (uint32_t i = 0; i < output_count; ++i) {
        if (outputs[i].enabled) {
            running = true;
            break;
        }
    }
    return Status::Ok;
}

bool Controller::is_initialized() const { return initialized; }
bool Controller::is_running() const { return running; }

bool is_amd_display_device(const PciIdentity& pci)
{
    return pci.vendor == 0x1002 && pci.class_code == 0x03;
}

bool is_dcn6_candidate(const PciIdentity& pci)
{
    return is_amd_display_device(pci) && pci.dcn_ip_major == 6;
}

Controller dcn6;

} // namespace amd_dcn6
