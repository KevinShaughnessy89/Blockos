#include <cstdlib>
#include <cstring>

#include "X11/Xlib.h"
#include "X11/Xrandr.h"
#include "X11/extensions/Xinerama.h"
#include "X11/extensions/Xrender.h"
#include "X11/extensions/shape.h"
#include "X11/extensions/Xfixes.h"

extern "C" {

// ============================================================
// XRandR
// ============================================================

Bool XRRQueryVersion(
    Display* dpy,
    int* major,
    int* minor)
{
    (void)dpy;

    if (major)
        *major = 1;

    if (minor)
        *minor = 6;

    return True;
}

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
// Xinerama
// ============================================================

Bool XineramaQueryExtension(
    Display* dpy,
    int* event_base,
    int* error_base)
{
    (void)dpy;

    if (event_base)
        *event_base = 0;

    if (error_base)
        *error_base = 0;

    return True;
}

Bool XineramaIsActive(
    Display* dpy,
    Window window)
{
    (void)dpy;
    (void)window;

    /*
     * BX11 currently exposes one virtual display.
     *
     * Xinerama is reported as active so legacy X11
     * window managers can obtain the screen geometry.
     */
    return True;
}

XineramaScreenInfo* XineramaQueryScreens(
    Display* dpy,
    int* count)
{
    (void)dpy;

    if (!count)
        return nullptr;

    *count = 1;

    XineramaScreenInfo* info =
        static_cast<XineramaScreenInfo*>(
            std::calloc(1, sizeof(XineramaScreenInfo)));

    if (!info)
    {
        *count = 0;
        return nullptr;
    }

    info->screen_number = 0;

    info->x_org = 0;
    info->y_org = 0;

    /*
     * Current BX11 virtual framebuffer geometry.
     */
    info->width = 1024;
    info->height = 768;

    return info;
}


// ============================================================
// XShape
// ============================================================

Bool XShapeQueryExtension(
    Display* dpy,
    int* event_base,
    int* error_base)
{
    (void)dpy;

    if (event_base)
        *event_base = 0;

    if (error_base)
        *error_base = 0;

    return True;
}

int XShapeCombineMask(
    Display* dpy,
    Window dest,
    int dest_kind,
    int x_off,
    int y_off,
    Pixmap src,
    int op)
{
    (void)dpy;
    (void)dest;
    (void)dest
