#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knipser.h"
#include "tray.h"
#include "wayland.h"

static sd_bus_slot *dbusSlot = NULL;
static sd_bus *dbusConnection = NULL;

// Callback for context menu activation
int on_context_menu(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	int x, y;
	int ret = sd_bus_message_read(m, "ii", &x, &y);
	if (ret < 0) {
		fprintf(stderr, "Failed to parse ContextMenu arguments: %s\n",
			strerror(-ret));
		return ret;
	}
	printf("Display in (%d,%d): %s", x, y, get_display_name_for_coordinates(x,y));
	knipser_handle_screenshot(x, y);

	return sd_bus_reply_method_return(m, "");
}

// Getter for D-Bus properties
int get_property(sd_bus *bus, const char *path, const char *interface,
		 const char *property, sd_bus_message *reply, void *userdata,
		 sd_bus_error *ret_error)
{
	if (strcmp(property, "Category") == 0) {
		return sd_bus_message_append(reply, "s", "ApplicationStatus");
	} else if (strcmp(property, "Id") == 0) {
		return sd_bus_message_append(reply, "s", "knipser");
	} else if (strcmp(property, "Title") == 0) {
		return sd_bus_message_append(reply, "s", "Knipser");
	} else if (strcmp(property, "Status") == 0) {
		return sd_bus_message_append(reply, "s", "Active");
	} else if (strcmp(property, "IconName") == 0) {
		return sd_bus_message_append(reply, "s", "camera-photo");
	}

	return -1; // Unknown property
}

const sd_bus_vtable tray_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Category", "s", get_property, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Id", "s", get_property, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Title", "s", get_property, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Status", "s", get_property, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("IconName", "s", get_property, 0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_METHOD("ContextMenu", "ii", "", on_context_menu,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};

int init_tray(void)
{
	int ret;

	// Connect to D-Bus session bus
	ret = sd_bus_default_user(&dbusConnection);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to D-Bus: %s\n",
			strerror(-ret));
		return 1;
	}

	// Export the StatusNotifierItem interface
	ret = sd_bus_add_object_vtable(dbusConnection, &dbusSlot,
				       "/knipser/tray",
				       "org.kde.StatusNotifierItem",
				       tray_vtable, NULL);

	if (ret < 0) {
		fprintf(stderr, "Failed to export methods: %s\n",
			strerror(-ret));
		return 1;
	}

	// Request ownership of the StatusNotifier service
	ret = sd_bus_request_name(dbusConnection, "org.knipser.Tray", 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to acquire D-Bus name: %s\n",
			strerror(-ret));
		return 1;
	}

	// Notify the StatusNotifierWatcher
	ret = sd_bus_call_method(dbusConnection,
				 "org.kde.StatusNotifierWatcher", // Destination
				 "/StatusNotifierWatcher", // Path
				 "org.kde.StatusNotifierWatcher", // Interface
				 "RegisterStatusNotifierItem", // Method
				 NULL, NULL, "s", "/knipser/tray");
	if (ret < 0) {
		fprintf(stderr,
			"Failed to register with StatusNotifierWatcher: %s\n",
			strerror(-ret));
		return 1;
	}

	while (1) {
		sd_bus_wait(dbusConnection, 2000);
		int ret = sd_bus_process(dbusConnection, NULL);
		if (ret < 0) {
			fprintf(stderr, "Error processing bus: %s (errno %d)\n",
				strerror(-ret), -ret);
			return -1;
		} else if (ret == 0) {
			break;
		}
	}

	printf("SNI tray icon running...\n");
	return 0;
}

void handle_sd_process()
{
	if ((dbusConnection != NULL) && sd_bus_is_open(dbusConnection)) {
		while (1) {
			sd_bus_wait(dbusConnection, UINT64_MAX);
			sd_bus_process(dbusConnection, NULL);
		}
	}
}

void deinit_tray(void)
{
	// Clean up the D-Bus slot if it exists
	if (dbusSlot) {
		sd_bus_slot_unref(dbusSlot);
		dbusSlot = NULL;
	}

	// Close and unref the D-Bus connection if it exists
	if (dbusConnection && sd_bus_is_open(dbusConnection)) {
		sd_bus_flush(dbusConnection); // Flush any pending messages
		sd_bus_close(dbusConnection);
		sd_bus_unref(dbusConnection);
		dbusConnection = NULL;
	}

	printf("Tray icon cleaned up.\n");
}