#include <stdio.h>

#include "wayland.h"
#include "tray.h"


int main(int argc, char *argv[])
{
	int ret = 0;
	init_wayland();

	ret = init_tray();

	if (ret != 0) {
		printf("Failed to init tray!\n");
	}

	while(1) {
		handle_sd_process();
	}

	deinit_tray();
	return 0;
}