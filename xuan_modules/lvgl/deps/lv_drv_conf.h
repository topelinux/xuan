#if 1
#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H
#include "lv_conf.h"

/* Use SDL driver instead of monitor/mouse/keyboard */
#ifndef USE_SDL
#define USE_SDL 1
#endif
#if USE_SDL
#define SDL_HOR_RES 800
#define SDL_VER_RES 600
#define SDL_ZOOM 1
#define SDL_DOUBLE_BUFFERED 1
#define SDL_INCLUDE_PATH "SDL2/SDL.h"
#endif

#ifndef USE_MOUSE
#define USE_MOUSE 0
#endif
#ifndef USE_KEYBOARD
#define USE_KEYBOARD 0
#endif
#ifndef USE_MOUSEWHEEL
#define USE_MOUSEWHEEL 0
#endif

#ifndef USE_FBDEV
#define USE_FBDEV 0
#endif
#if USE_FBDEV
#ifndef FBDEV_PATH
#define FBDEV_PATH "/dev/fb0"
#endif
#endif

#ifndef USE_EVDEV
#define USE_EVDEV 0
#endif
#if USE_EVDEV
#ifndef EVDEV_NAME
#define EVDEV_NAME "/dev/input/event0"
#endif
#ifndef EVDEV_SWAP_AXES
#define EVDEV_SWAP_AXES 0
#endif
#ifndef EVDEV_CALIBRATE
#define EVDEV_CALIBRATE 0
#endif
#ifndef EVDEV_HOR_MIN
#define EVDEV_HOR_MIN 0
#endif
#ifndef EVDEV_HOR_MAX
#define EVDEV_HOR_MAX 4096
#endif
#ifndef EVDEV_VER_MIN
#define EVDEV_VER_MIN 0
#endif
#ifndef EVDEV_VER_MAX
#define EVDEV_VER_MAX 4096
#endif
#endif

#endif
#endif
