#pragma once
#include <stdint.h>
#include <stddef.h>

namespace media_camera {

// USB Video Class (UVC) format identifiers.
constexpr uint32_t PIXEL_MJPEG = 0x47504A4D; // 'MJPG'
constexpr uint32_t PIXEL_YUY2  = 0x32595559; // 'YUY2'

struct Format { uint32_t width,height,pixel_format,fps; };
struct DeviceInfo { uint8_t bus,port,address; uint16_t vendor_id,product_id; bool streaming; };

bool init();
bool available();
bool configure(const Format& format);
bool probe(DeviceInfo& out);
const Format& current_format();

// Feed one complete UVC video payload. A transport/xHCI layer can call this
// from its isochronous completion path; the driver validates the UVC header
// and strips it before exposing frame payload bytes.
typedef void (*FrameCallback)(const uint8_t* data,size_t bytes,bool frame_end);
void set_frame_callback(FrameCallback cb);
bool submit_payload(const uint8_t* payload,size_t bytes);

} // namespace media_camera
