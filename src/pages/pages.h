/*
 * Page declarations for NanoHat OLED UI
 */
#ifndef PAGES_H
#define PAGES_H

#include "../page.h"
/* Declare all pages */
extern const page_t page_home;
extern const page_t page_gateway;
extern const page_t page_network;
extern const page_t page_services;
extern const page_t page_settings;

/* Page list for controller registration */
static inline const page_t **pages_get_list(int *count) {
    static const page_t *pages[] = {
        &page_home,
        &page_gateway,
        &page_network,
        &page_services,
        &page_settings,
    };
    *count = sizeof(pages) / sizeof(pages[0]);
    return pages;
}

#endif
