#ifndef _TRAY_H_
#define _TRAY_H_

#include <systemd/sd-bus.h>

extern const sd_bus_vtable tray_vtable[];

int init_tray(void);
void handle_sd_process(void);
void deinit_tray(void);

#endif /* _TRAY_H_ */
