/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <png.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "wayland-protocols/wlr-screencopy-unstable-v1-client-protocol.h"
#include "wayland-protocols/wlr-output-management-unstable-v1-client-protocol.h"

#define MAX_NUM_WAYLAND_DISPLAYS 64

// Forward declarations
static const struct zwlr_output_manager_v1_listener output_manager_listener;
static const struct zwlr_output_mode_v1_listener output_mode_listener;
static const struct zwlr_output_head_v1_listener output_head_listener;
static const struct wl_output_listener output_listener;

// Global Wayland state
static struct wl_shm *shm = NULL;
static struct zwlr_screencopy_manager_v1 *screencopy_manager = NULL;
static struct wl_output *output = NULL;
static struct zwlr_output_manager_v1 *output_manager = NULL;
static uint32_t serial = 0;
static struct wl_list output_heads;  // List of output_head structures

static struct {
    struct wl_buffer *wl_buffer;
    void *data;
    enum wl_shm_format format;
    int width, height, stride;
    bool y_invert;
} buffer;

static bool buffer_copy_done = false;

struct {
    struct wl_display *display;
    struct wl_registry *registry;
} wl_state;

struct output_head {
    struct zwlr_output_head_v1 *wlr_head;
    struct wl_output *wl_output;  // Add standard wl_output
    struct wl_list link;
    char *name;
    char *description;
    int32_t x, y;
    int32_t width, height;
    int32_t enabled;
    struct zwlr_output_mode_v1 *current_mode;
};

struct output_head display_list[MAX_NUM_WAYLAND_DISPLAYS] = {0};
static int current_num_displays = 0;

// Function prototypes
static struct wl_buffer *create_shm_buffer(enum wl_shm_format fmt, int width, int height, int stride, void **data_out);
static void write_image(const char *filename, enum wl_shm_format wl_fmt, int width, int height, int stride, bool y_invert, png_bytep data);
struct output_head *find_output_for_coordinates(int32_t x, int32_t y);
const char *get_display_name_for_coordinates(int32_t x, int32_t y);

// Frame listener callbacks
static void frame_handle_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride)
{
    buffer.format = format;
    buffer.width = width;
    buffer.height = height;
    buffer.stride = stride;

    // Make sure the buffer is not allocated
    assert(!buffer.wl_buffer);
    buffer.wl_buffer = create_shm_buffer(format, width, height, stride, &buffer.data);
    if (buffer.wl_buffer == NULL) {
        fprintf(stderr, "Failed to create buffer\n");
        exit(EXIT_FAILURE);
    }

    zwlr_screencopy_frame_v1_copy(frame, buffer.wl_buffer);
}

static void frame_handle_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags)
{
    buffer.y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
}

static void frame_handle_ready(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    buffer_copy_done = true;
}

static void frame_handle_failed(void *data, struct zwlr_screencopy_frame_v1 *frame)
{
    fprintf(stderr, "Failed to copy frame\n");
    exit(EXIT_FAILURE);
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
};

// Registry listener callbacks
static void handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (strcmp(interface, wl_output_interface.name) == 0) {
        // Bind all wl_output objects we find
        struct wl_output *wl_output = wl_registry_bind(registry, name, &wl_output_interface, 2);

        // Save a reference to the first one for screen capture
        if (output == NULL) {
            output = wl_output;
        }

        // Create a new output head for each wl_output
        struct output_head *head = calloc(1, sizeof(struct output_head));
        if (head) {
            head->wl_output = wl_output;
            head->enabled = 1;  // Assume enabled by default
            wl_list_insert(&output_heads, &head->link);

            // Add the standard output listener
            wl_output_add_listener(wl_output, &output_listener, head);
        }
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        screencopy_manager = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    } else if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        output_manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 1);
        zwlr_output_manager_v1_add_listener(output_manager, &output_manager_listener, NULL);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    // No-op
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

// Create shared memory buffer
static struct wl_buffer *create_shm_buffer(enum wl_shm_format fmt, int width, int height, int stride, void **data_out)
{
    int size = stride * height;

    const char shm_name[] = "/wlroots-screencopy";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        fprintf(stderr, "shm_open failed\n");
        return NULL;
    }
    shm_unlink(shm_name);

    int ret;
    while ((ret = ftruncate(fd, size)) == EINTR) {
        // No-op
    }
    if (ret < 0) {
        close(fd);
        fprintf(stderr, "ftruncate failed\n");
        return NULL;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    close(fd);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, fmt);
    wl_shm_pool_destroy(pool);

    *data_out = data;
    return buffer;
}

// Write image to file
static void write_image(const char *filename, enum wl_shm_format wl_fmt, int width, int height, int stride, bool y_invert, png_bytep data)
{
    const struct format {
        enum wl_shm_format wl_format;
        bool is_bgr;
    } formats[] = {
        { WL_SHM_FORMAT_XRGB8888, true },
        { WL_SHM_FORMAT_ARGB8888, true },
        { WL_SHM_FORMAT_XBGR8888, false },
        { WL_SHM_FORMAT_ABGR8888, false },
    };

    const struct format *fmt = NULL;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        if (formats[i].wl_format == wl_fmt) {
            fmt = &formats[i];
            break;
        }
    }
    if (fmt == NULL) {
        fprintf(stderr, "Unsupported format %" PRIu32 "\n", wl_fmt);
        exit(EXIT_FAILURE);
    }

    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        fprintf(stderr, "Failed to open output file\n");
        exit(EXIT_FAILURE);
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);

    png_init_io(png, f);

    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    if (fmt->is_bgr) {
        png_set_bgr(png);
    }

    png_write_info(png, info);

    for (size_t i = 0; i < (size_t)height; ++i) {
        png_bytep row;
        if (y_invert) {
            row = data + (height - i - 1) * stride;
        } else {
            row = data + i * stride;
        }
        png_write_row(png, row);
    }

    png_write_end(png, NULL);

    png_destroy_write_struct(&png, &info);

    fclose(f);
}

// Initialize Wayland
int init_wayland(void)
{
    wl_list_init(&output_heads);

    // Connect to the Wayland display
    wl_state.display = wl_display_connect(NULL);
    if (!wl_state.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return EXIT_FAILURE;
    }

    wl_state.registry = wl_display_get_registry(wl_state.display);
    wl_registry_add_listener(wl_state.registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_state.display); // Ensure registry is populated
    wl_display_roundtrip(wl_state.display); // Another roundtrip for output heads

    if (!shm) {
        fprintf(stderr, "Compositor is missing wl_shm\n");
        return EXIT_FAILURE;
    }
    if (!screencopy_manager) {
        fprintf(stderr, "Compositor doesn't support wlr-screencopy-unstable-v1\n");
        return EXIT_FAILURE;
    }
    if (!output) {
        fprintf(stderr, "No output available\n");
        return EXIT_FAILURE;
    }
    if (!output_manager) {
        fprintf(stderr, "Compositor doesn't support wlr-output-management-unstable-v1\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}



// Output head listener callbacks
static void output_head_handle_name(void *data, struct zwlr_output_head_v1 *wlr_head, const char *name)
{
    struct output_head *head = data;
    head->name = strdup(name);
}

static void output_head_handle_description(void *data, struct zwlr_output_head_v1 *wlr_head, const char *description)
{
    struct output_head *head = data;
    head->description = strdup(description);
}

static void output_head_handle_physical_size(void *data, struct zwlr_output_head_v1 *wlr_head, int32_t width, int32_t height)
{
    // Not needed for our purpose
}

static void output_head_handle_mode(void *data, struct zwlr_output_head_v1 *wlr_head, struct zwlr_output_mode_v1 *mode)
{
    struct output_head *head = data;

    // Register the listener for this mode
    zwlr_output_mode_v1_add_listener(mode, &output_mode_listener, data);
}

static void output_head_handle_enabled(void *data, struct zwlr_output_head_v1 *wlr_head, int32_t enabled)
{
    struct output_head *head = data;
    head->enabled = enabled;
}

static void output_head_handle_current_mode(void *data, struct zwlr_output_head_v1 *wlr_head, struct zwlr_output_mode_v1 *mode)
{
    struct output_head *head = data;

    // First store the current mode pointer
    head->current_mode = mode;

    // Then register the listener for this mode
    zwlr_output_mode_v1_add_listener(mode, &output_mode_listener, data);

    printf("Current mode set for %s at position (%d,%d)\n",
           head->name ? head->name : "(unnamed)", head->x, head->y);
}

static void output_head_handle_position(void *data, struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y)
{
    struct output_head *head = data;
    head->x = x;
    head->y = y;
    printf("Position: %d, %d\n", x, y);
}

static void output_head_handle_transform(void *data, struct zwlr_output_head_v1 *wlr_head, int32_t transform)
{
    // Not needed for our purpose
}

static void output_head_handle_scale(void *data, struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale)
{
    // Not needed for our purpose
}

static void output_head_handle_finished(void *data, struct zwlr_output_head_v1 *wlr_head)
{
    // This head has been removed
    struct output_head *head = data;
    for(int i = 0; i < current_num_displays; i++) {
        if(strcmp(display_list[i].name, "")) {
            memcpy(&display_list[i], head, sizeof(struct output_head));
	    display_list[i].name = strdup(head->name);
        display_list[i].description = strdup(head->description);
	}
    }
    wl_list_remove(&head->link);
    free(head->name);
    free(head->description);
    free(head);
}

static void output_mode_handle_size(void *data,
                                   struct zwlr_output_mode_v1 *wlr_mode,
                                   int32_t width, int32_t height)
{
    struct output_head *head = data;
    head->width = width;
    head->height = height;

    printf("Current mode size for %s: %dx%d\n",
           head->name ? head->name : "(unnamed)",
           width, height);

}

static void output_mode_handle_refresh(void *data,
				       struct zwlr_output_mode_v1 *wlr_mode,
				       int32_t refresh)
{
	// We don't need to store the refresh rate for coordinate lookups,
	// but we need to handle the event
	(void)data;
	(void)wlr_mode;
	(void)refresh;
}

static void output_mode_handle_preferred(void *data, struct zwlr_output_mode_v1 *wlr_mode)
{
    // This is called when a mode is marked as preferred
    // We don't need any specific handling for this event
    (void)data;
    (void)wlr_mode;
}

static void output_mode_handle_finished(void *data, struct zwlr_output_mode_v1 *wlr_mode)
{
    // This mode has been removed
    // We don't need specific cleanup as the mode will be destroyed by the compositor
    (void)data;
    (void)wlr_mode;
}

// Update the mode listener to use all callbacks
static const struct zwlr_output_mode_v1_listener output_mode_listener = {
    .size = output_mode_handle_size,
    .refresh = output_mode_handle_refresh,
    .preferred = output_mode_handle_preferred,
    .finished = output_mode_handle_finished
};

// Additional callbacks for output_head_listener
// These are for opcodes 10-13 that might be supported in newer protocols

static void output_head_handle_make(void *data, struct zwlr_output_head_v1 *wlr_head, const char *make)
{
    // Handle the make information (manufacturer)
    // Added in protocol version 2
    (void)data;
    (void)wlr_head;
    (void)make;
}

static void output_head_handle_model(void *data, struct zwlr_output_head_v1 *wlr_head, const char *model)
{
    // Handle the model information
    // Added in protocol version 2
    (void)data;
    (void)wlr_head;
    (void)model;
}

static void output_head_handle_serial_number(void *data, struct zwlr_output_head_v1 *wlr_head, const char *serial_number)
{
    // Handle the serial number information
    // Added in protocol version 2
    (void)data;
    (void)wlr_head;
    (void)serial_number;
}

static void output_head_handle_adaptive_sync(void *data, struct zwlr_output_head_v1 *wlr_head, uint32_t state)
{
    // Handle adaptive sync state changes
    // Added in protocol version 4
    (void)data;
    (void)wlr_head;
    (void)state;
}

static void output_manager_handle_finished(void *data, struct zwlr_output_manager_v1 *manager)
{
    zwlr_output_manager_v1_destroy(manager);
    output_manager = NULL;
}

static void output_manager_handle_head(void *data,
				       struct zwlr_output_manager_v1 *manager,
				       struct zwlr_output_head_v1 *wlr_head)
{
    // Look for existing output_head with wl_output already set
    struct output_head *head;
    struct output_head *new_head = NULL;

    // First try to find if we have already created an output_head for a corresponding wl_output
    wl_list_for_each(head, &output_heads, link) {
        if (head->wlr_head == NULL) {
            // This head was created from wl_output but doesn't have wlr_head yet
            new_head = head;
            break;
        }
    }

    // If no matching head was found, create a new one
    if (!new_head) {
        new_head = calloc(1, sizeof(struct output_head));
        if (!new_head) {
            fprintf(stderr, "Failed to allocate output head\n");
            return;
        }
        wl_list_insert(&output_heads, &new_head->link);
    }

    // Add WLR head info
    new_head->wlr_head = wlr_head;

    // Add listener for WLR output events
    zwlr_output_head_v1_add_listener(wlr_head, &output_head_listener, new_head);
}

static void output_manager_handle_done(void *data,
				       struct zwlr_output_manager_v1 *manager,
				       uint32_t serial_arg)
{
	serial = serial_arg;
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
    .head = output_manager_handle_head,
    .done = output_manager_handle_done,
    .finished = output_manager_handle_finished,
};

// For protocol version 4, include all callbacks
static const struct zwlr_output_head_v1_listener output_head_listener = {
    .name = output_head_handle_name,
    .description = output_head_handle_description,
    .physical_size = output_head_handle_physical_size,
    .mode = output_head_handle_mode,
    .enabled = output_head_handle_enabled,
    .current_mode = output_head_handle_current_mode,
    .position = output_head_handle_position,
    .transform = output_head_handle_transform,
    .scale = output_head_handle_scale,
    .finished = output_head_handle_finished,
    .make = output_head_handle_make,
    .model = output_head_handle_model,
    .serial_number = output_head_handle_serial_number,
    .adaptive_sync = output_head_handle_adaptive_sync
};

// Standard wl_output listener callbacks
static void handle_output_geometry(void *data, struct wl_output *wl_output,
                                  int32_t x, int32_t y, int32_t phys_width,
                                  int32_t phys_height, int32_t subpixel,
                                  const char *make, const char *model,
                                  int32_t transform)
{
    return;
}

static void handle_output_mode(void *data, struct wl_output *wl_output,
                              uint32_t flags, int32_t width, int32_t height,
                              int32_t refresh)
{
    // Only use current mode
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;

    struct output_head *head = data;
    head->width = width;
    head->height = height;

    printf("Standard output mode for %s: %dx%d (current)\n",
           head->name ? head->name : "(unnamed)", width, height);
}

static void handle_output_done(void *data, struct wl_output *wl_output)
{
    struct output_head *head = data;
    printf("Standard output done for %s: %dx%d at (%d,%d)\n",
           head->name ? head->name : "(unnamed)",
           head->width, head->height, head->x, head->y);
}

static void handle_output_scale(void *data, struct wl_output *wl_output, int32_t scale)
{
    // Not needed for coordinate lookup
    (void)data;
    (void)wl_output;
    (void)scale;
}

static const struct wl_output_listener output_listener = {
    .geometry = handle_output_geometry,
    .mode = handle_output_mode,
    .done = handle_output_done,
    .scale = handle_output_scale
};

// Function to find the display containing specific coordinates
struct output_head *find_output_for_coordinates(int32_t x, int32_t y)
{
    struct output_head *head;
    struct output_head *nearest_head = NULL;
    int32_t nearest_distance = INT32_MAX;

    printf("Looking for coordinates (%d,%d)\n", x, y);

    wl_list_for_each(head, &output_heads, link) {
        if (!head->enabled) {
            continue;
        }

        // Use fallback dimensions if width/height are zero
        int32_t width = (head->width > 0) ? head->width : 1920;
        int32_t height = (head->height > 0) ? head->height : 1080;

        printf("Checking display %s: bounds (%d,%d)-(%d,%d)\n",
               head->name ? head->name : "(unnamed)",
               head->x, head->y,
               head->x + width, head->y + height);

        // Check if the coordinates are within this output's boundaries
        if (x >= head->x && x < head->x + width &&
            y >= head->y && y < head->y + height) {
            printf("Found coordinates on display: %s\n",
                head->name ? head->name : "(unnamed)");
            return head;
        }

        // Calculate distance to center of display for fallback
        int32_t center_x = head->x + (width / 2);
        int32_t center_y = head->y + (height / 2);
        int32_t dx = x - center_x;
        int32_t dy = y - center_y;
        int32_t distance = dx*dx + dy*dy;

        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_head = head;
        }
    }

    // If no exact match, return the nearest display
    printf("No exact match found, using nearest display: %s\n",
           nearest_head ? (nearest_head->name ? nearest_head->name : "(unnamed)") : "none");
    return nearest_head;
}

// Helper function to get display name for coordinates
const char *get_display_name_for_coordinates(int32_t x, int32_t y)
{
    struct output_head *head = find_output_for_coordinates(x, y);
    if (head) {
        return head->name;
    }
    return NULL;
}

// Take a screenshot
int take_screenshot(const char *filename, int x, int y)
{
    struct output_head *display_meta = find_output_for_coordinates(x, y);
    if (display_meta == NULL) {
        printf("failed getting output for screenshot");
        return -1;
    }
	struct zwlr_screencopy_frame_v1 *frame =
		zwlr_screencopy_manager_v1_capture_output(screencopy_manager, 0,
							  display_meta->wl_output);
	zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, NULL);

	buffer_copy_done = false;
	while (!buffer_copy_done &&
	       wl_display_dispatch(wl_state.display) != -1) {
		// Wait for the frame to be copied
	}

	write_image(filename, buffer.format, buffer.width, buffer.height,
		    buffer.stride, buffer.y_invert, buffer.data);

	// Clean up the buffer
	wl_buffer_destroy(buffer.wl_buffer);
	munmap(buffer.data, buffer.stride * buffer.height);
	buffer.wl_buffer = NULL; // Reset the buffer to NULL to avoid reuse

	return EXIT_SUCCESS;
}

