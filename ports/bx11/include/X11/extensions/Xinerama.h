#pragma once

#include "../Xlib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int screen_number;

    short x_org;
    short y_org;

    unsigned short width;
    unsigned short height;

} XineramaScreenInfo;

Bool XineramaQueryExtension(
    Display*,
    int*,
    int*
);

Bool XineramaIsActive(
    Display*,
    Window
);

XineramaScreenInfo* XineramaQueryScreens(
    Display*,
    int*
);

#ifdef __cplusplus
}
#endif
