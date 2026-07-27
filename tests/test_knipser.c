#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Mock dependencies
int take_screenshot(const char *filename, int x, int y) {
    (void)filename;
    (void)x;
    (void)y;
    return 0;
}

// Include the C file to test static functions
#include "../knipser.c"

void test_parse_config_file() {
    // Write test config file
    FILE *f = fopen("test_knipser.toml", "w");
    assert(f != NULL);
    fprintf(f, "save_directory = \"/tmp/test_dir\"\n");
    fclose(f);

    // Call function
    parse_config_file("test_knipser.toml");

    // Check result
    assert(strcmp(g_config.save_directory, "/tmp/test_dir") == 0);

    // Cleanup
    remove("test_knipser.toml");
}

int main() {
    test_parse_config_file();
    printf("All knipser tests passed!\\n");
    return 0;
}
