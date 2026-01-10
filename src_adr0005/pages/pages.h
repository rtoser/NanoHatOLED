/*
 * Page declarations for NanoHat OLED UI
 */
#ifndef PAGES_H
#define PAGES_H

#include "../page.h"
#include "../task_queue.h"

/* Declare all pages */
extern const page_t page_home;
extern const page_t page_network;
extern const page_t page_gateway;
extern const page_t page_services;

/* Page list for controller registration */
static inline const page_t **pages_get_list(int *count) {
    static const page_t *pages[] = {
        &page_home,
        &page_network,
        &page_gateway,
        &page_services,
    };
    *count = sizeof(pages) / sizeof(pages[0]);
    return pages;
}

/* Service page helper to set task queue */
void page_services_set_task_queue(task_queue_t *tq);

#endif
