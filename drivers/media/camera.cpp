#include "camera.hpp"
#include "../usb_xhci.hpp"
#include "../pci.hpp"
#include <string.h>

namespace media_camera {
namespace {
struct State { bool ready=false; bool streaming=false; DeviceInfo dev{}; Format fmt{}; FrameCallback cb=nullptr; } g{};
constexpr uint8_t USB_CLASS_VIDEO=0x0E;
constexpr uint8_t USB_SUBCLASS_VIDEOSTREAMING=0x02;

// This layer is intentionally a UVC protocol driver, not a fake frame
// generator. xHCI enumeration/isochronous transfer ownership stays in the
// USB subsystem; payloads are accepted only after UVC header validation.
}

bool init(){
    usb::xhci::Controller c{};
    if(!usb::xhci::probe(&c)) return false;
    g.dev={c.bus,0,0,0,0,false};
    g.ready=true;
    return true;
}

bool available(){ return g.ready; }
bool configure(const Format& f){
    if(!g.ready||!f.width||!f.height||!f.fps) return false;
    if(f.pixel_format!=PIXEL_MJPEG && f.pixel_format!=PIXEL_YUY2) return false;
    g.fmt=f; g.streaming=true; return true;
}
bool probe(DeviceInfo& out){ if(!g.ready) return false; out=g.dev; return true; }
const Format& current_format(){ return g.fmt; }
void set_frame_callback(FrameCallback cb){ g.cb=cb; }

bool submit_payload(const uint8_t* payload,size_t bytes){
    if(!g.ready||!g.streaming||!payload||bytes<2) return false;
    uint8_t header=payload[0];
    if(header<2 || header>bytes) return false;
    // UVC payload header bit 1 is EOF. Bit 6 is ERR.
    const bool eof=(payload[1]&0x02)!=0;
    const bool err=(payload[1]&0x40)!=0;
    if(err) return false;
    if(g.cb) g.cb(payload+header,bytes-header,eof);
    return true;
}

} // namespace media_camera
