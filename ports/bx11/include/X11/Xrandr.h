#pragma once

#include "Xlib.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// XRandR basic types
// ============================================================

typedef unsigned long RROutput;
typedef unsigned long RRCrtc;
typedef unsigned long RRMode;
typedef unsigned long RROutput;
typedef unsigned long XRRModeInfoID;

// ============================================================
// Legacy screen size
// ============================================================

typedef struct
{
    unsigned long flags;

    int x;
    int y;

    int width;
    int height;

    int mwidth;
    int mheight;

} XRRScreenSize;

// ============================================================
// XRandR monitor information
// ============================================================

typedef struct
{
    Atom name;

    Bool primary;
    Bool automatic;

    int x;
    int y;

    int width;
    int height;

    int mwidth;
    int mheight;

    int noutput;
    RROutput* outputs;

} XRRMonitorInfo;

// ============================================================
// Screen change event
// ============================================================

typedef struct
{
    int type;

    unsigned long serial;

    Bool send_event;

    Display* display;

    Window window;

    int rotation;

    Time timestamp;
    Time config_timestamp;

    unsigned long sizeID;

    unsigned short subpixel_order;

} XRRScreenChangeNotifyEvent;

// ============================================================
// XRandR extension
// ============================================================

Bool XRRQueryExtension(
    Display*,
    int*,
    int*
);

Status XRRQueryVersion(
    Display*,
    int*,
    int*
);

// ============================================================
// Monitor API
// ============================================================

XRRMonitorInfo* XRRGetMonitors(
    Display*,
    Window,
    Bool,
    int*
);

// ============================================================
// Legacy configuration API
// ============================================================

void* XRRGetScreenInfo(
    Display*,
    Window
);

XRRScreenSize* XRRConfigSizes(
    void*,
    int*
);

short XRRConfigCurrentConfiguration(
    void*,
    int*
);

void XRRFreeScreenConfigInfo(
    void*
);

int XRRSelectInput(
    Display*,
    Window,
    int
);

#ifdef __cplusplus
}
#endif
