#ifndef MOON_HEADLESS_GRAPHICS_H
#define MOON_HEADLESS_GRAPHICS_H
#include <windows.h>
#include <stdbool.h>
/* Suppress GPU-only progress textures while preserving CPU progress initialization. */
bool moon_install_headless_graphics(HMODULE module, void (*log_message)(const char *));
void moon_headless_frame_limit(void);
#endif
