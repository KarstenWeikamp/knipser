#include <stdio.h>
#include <time.h>

#include "wayland.h"
#include "knipser.h"
#include <string.h>
#include <stdlib.h>

struct knipser_config g_config = {
    .save_directory = "."
};

static void parse_config_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "save_directory", 14) == 0) {
            char *p = line + 14;
            while (*p == ' ' || *p == '=') p++;
            if (*p == '"') {
                p++;
                char *end = strchr(p, '"');
                if (end) *end = '\0';
                strncpy(g_config.save_directory, p, sizeof(g_config.save_directory) - 1);
                g_config.save_directory[sizeof(g_config.save_directory) - 1] = '\0';
            }
        }
    }
    fclose(f);
}

void load_config(void) {
    // Load system-wide config first
    parse_config_file("/etc/knipser/knipser.toml");

    // Load user config (overrides system config)
    const char *home = getenv("HOME");
    if (home) {
        char config_path[512];
        snprintf(config_path, sizeof(config_path), "%s/.config/knipser/knipser.toml", home);
        parse_config_file(config_path);
    }
}

void save_config(void) {
    char config_dir[512];
    char config_path[512];
    const char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(config_dir, sizeof(config_dir), "%s/.config/knipser", home);
    snprintf(config_path, sizeof(config_path), "%s/knipser.toml", config_dir);
    
    // Ensure directory exists (basic way, assume mkdir works or dir exists)
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", config_dir);
    system(cmd);

    FILE *f = fopen(config_path, "w");
    if (!f) return;
    
    fprintf(f, "save_directory = \"%s\"\n", g_config.save_directory);
    fclose(f);
}

int knipser_handle_screenshot(int cursor_x, int cursor_y) {
	time_t now;
	struct tm *tm_info;
	char timestamp[20]; // Enough for YYYY-MM-DDThh:mm:ss\0

	time(&now);
	tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm_info);

	char filename[512];
	if (strlen(g_config.save_directory) > 0 && strcmp(g_config.save_directory, ".") != 0) {
		snprintf(filename, sizeof(filename), "%s/screenshot_%s.png", g_config.save_directory, timestamp);
	} else {
		snprintf(filename, sizeof(filename), "screenshot_%s.png", timestamp);
	}
	take_screenshot(filename, cursor_x, cursor_y);
}