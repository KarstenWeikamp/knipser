#include <stdio.h>
#include <time.h>

#include "wayland.h"


int knipser_handle_screenshot(int cursor_x, int cursor_y) {
	time_t now;
	struct tm *tm_info;
	char timestamp[20]; // Enough for YYYY-MM-DDThh:mm:ss\0

	time(&now);
	tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm_info);

	char filename[40];
	sprintf(filename, "screenshot_%s.png", timestamp);
	take_screenshot(filename, cursor_x, cursor_y);
}