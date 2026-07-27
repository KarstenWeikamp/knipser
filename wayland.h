#ifndef _WAYLAND_H_
#define _WAYLAND_H_

#include <stdint.h>

enum ui_state_mode {
	UI_STATE_NONE = 0,
	UI_STATE_AREA_SELECTION,
	UI_STATE_TRAY_MENU
};

int init_wayland(void);
int take_screenshot(const char *, int, int);
const char *get_display_name_for_coordinates(int32_t x, int32_t y);
void show_tray_menu(int x, int y);

#endif /*ifndef _WAYLAND_H_*/