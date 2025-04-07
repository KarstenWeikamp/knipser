#ifndef _WAYLAND_H_
#define _WAYLAND_H_

#include <stdint.h>

void init_wayland(void);
int take_screenshot(const char *, int, int);
const char *get_display_name_for_coordinates(int32_t x, int32_t y);

#endif /*ifndef _WAYLAND_H_*/