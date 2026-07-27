#ifndef _KNIPSER_H_
#define _KNIPSER_H_

int knipser_handle_screenshot(int, int);

struct knipser_config {
    char save_directory[256];
};

extern struct knipser_config g_config;

void load_config(void);
void save_config(void);

#endif /*ifndef _KNIPSER_H_*/