#include <cstdlib>
#include <cstring>

#include "X11/Xlib.h"
#include "X11/Xrandr.h"
#include "X11/extensions/Xrender.h"

extern "C" {

// ============================================================
// XRandR
// ============================================================

Bool XRRQueryExtension(
    Display* dpy,
    int* event_base_return,
    int* error_base_return)
{
    (void)dpy;

    if (event_base_return)
        *event_base_return = 64;

    if (error_base_return)
        *error_base_return = 128;

    return True;
}

Status XRRQueryVersion(
    Display* dpy,
    int* major_version_return,
    int* minor_version_return)
{
    (void)dpy;

    /*
     * FVWM3 expects a reasonably modern XRandR version.
     * BX11 currently exposes a virtual single-monitor display.
     */
    if (major_version_return)
        *major_version_return = 1;

    if (minor_version_return)
        *minor_version_return = 5;

    return 1;
}

// ------------------------------------------------------------
// XRRGetMonitors
// ------------------------------------------------------------

XRRMonitorInfo* XRRGetMonitors(
    Display* dpy,
    Window window,
    Bool get_active,
    int* nmonitors)
{
    (void)dpy;
    (void)window;
    (void)get_active;

    if (!nmonitors)
        return nullptr;

    *nmonitors = 1;

    XRRMonitorInfo* monitors =
        static_cast<XRRMonitorInfo*>(
            std::calloc(1, sizeof(XRRMonitorInfo)));

    if (!monitors)
    {
        *nmonitors = 0;
        return nullptr;
    }

    /*
     * BX11 currently presents one virtual framebuffer
     * as the primary monitor.
     *
     * Current BX11 default framebuffer:
     *     1024 x 768
     */
    monitors[0].name = 0;
    monitors[0].primary = True;
    monitors[0].automatic = True;

    monitors[0].x = 0;
    monitors[0].y = 0;

    monitors[0].width = 1024;
    monitors[0].height = 768;

    /*
     * Physical monitor size in millimeters.
     * Approximate 12" 4:3 display.
     */
    monitors[0].mwidth = 270;
    monitors[0].mheight = 203;

    monitors[0].noutput = 0;
    monitors[0].outputs = nullptr;

    return monitors;
}

// ------------------------------------------------------------
// Legacy XRandR screen configuration API
// ------------------------------------------------------------

void* XRRGetScreenInfo(
    Display* dpy,
    Window window)
{
    (void)dpy;
    (void)window;

    return nullptr;
}

XRRScreenSize* XRRConfigSizes(
    void* config,
    int* n)
{
    (void)config;

    if (n)
        *n = 0;

    return nullptr;
}

short XRRConfigCurrentConfiguration(
    void* config,
    int* rotation)
{
    (void)config;

    if (rotation)
        *rotation = 0;

    return 0;
}

void XRRFreeScreenConfigInfo(
    void* config)
{
    (void)config;
}

int XRRSelectInput(
    Display* dpy,
    Window window,
    int mask)
{
    (void)dpy;
    (void)window;
    (void)mask;

    return 0;
}


// ============================================================
// XRender
// ============================================================

Bool XRenderQueryExtension(
    Display* dpy,
    int* event_base_return,
    int* error_base_return)
{
    (void)dpy;

    if (event_base_return)
        *event_base_return = 72;

    if (error_base_return)
        *error_base_return = 140;

    return True;
}

Status XRenderQueryVersion(
    Display* dpy,
    int* major_version_return,
    int* minor_version_return)
{
    (void)dpy;

    /*
     * Advertise XRender 0.11.
     * This is sufficient for the basic ARGB/alpha
     * functionality expected by many X11 applications.
     */
    if (major_version_return)
        *major_version_return = 0;

    if (minor_version_return)
        *minor_version_return = 11;

    return 1;
}

// ------------------------------------------------------------
// XRender visual format
// ------------------------------------------------------------

XRenderPictFormat* XRenderFindVisualFormat(
    Display* dpy,
    Visual* visual)
{
    (void)dpy;
    (void)visual;

    /*
     * BX11 framebuffer format:
     *
     * 32-bit ARGB
     *
     *     AAAAAAAA RRRRRRRR GGGGGGGG BBBBBBBB
     *
     * Alpha  = bits 24..31
     * Red    = bits 16..23
     * Green  = bits  8..15
     * Blue   = bits  0..7
     */

    static XRenderPictFormat format{};

    format.id = 1;
    format.type = PictTypeDirect;
    format.depth = 32;

    format.direct.red = 16;
    format.direct.redMask = 0xff;

    format.direct.green = 8;
    format.direct.greenMask = 0xff;

    format.direct.blue = 0;
    format.direct.blueMask = 0xff;

    format.direct.alpha = 24;
    format.direct.alphaMask = 0xff;

    return &format;
}

// ------------------------------------------------------------
// XRender picture cleanup
// ------------------------------------------------------------

void XRenderFreePicture(
    Display* dpy,
    Picture picture)
{
    (void)dpy;
    (void)picture;
}

} // extern "C"
