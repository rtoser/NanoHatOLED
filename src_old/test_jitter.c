#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>

#define GPIO_PATH "/sys/class/gpio"
#define BTN_GPIO 0  // Testing K1 (GPIO 0)

static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char **argv) {
    char path[64];
    int fd;
    struct pollfd pfd;
    char val;
    uint64_t start_time, now;
    int count = 0;

    printf("Starting jitter test on GPIO %d...\n", BTN_GPIO);

    // Export
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_PATH, BTN_GPIO);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open GPIO value");
        // Try to export if failed
        int export_fd = open(GPIO_PATH "/export", O_WRONLY);
        if (export_fd >= 0) {
            dprintf(export_fd, "%d", BTN_GPIO);
            close(export_fd);
            usleep(100000);
            fd = open(path, O_RDONLY);
        }
    }

    if (fd < 0) {
        perror("Still failed to open GPIO");
        return 1;
    }

    start_time = get_time_us();

    pfd.fd = fd;
    pfd.events = POLLPRI | POLLERR;

    while (1) {
        // Seek to clear interrupt
        lseek(fd, 0, SEEK_SET);
        read(fd, &val, 1);
        
        // Wait for edge
        int ret = poll(&pfd, 1, 5000); // 5s timeout
        
        now = get_time_us();
        
        if (ret > 0) {
            // Read immediate value
            lseek(fd, 0, SEEK_SET);
            read(fd, &val, 1);
            
            printf("[%8llu us] EVENT: val=%c (poll_revents=0x%x)\n", 
                   (now - start_time), val, pfd.revents);
            
            // Burst read to see if it changes rapidly
            for(int i=0; i<5; i++) {
                lseek(fd, 0, SEEK_SET);
                read(fd, &val, 1);
                printf("    + %d us: %c\n", i*100, val);
                usleep(100);
            }
        } else if (ret == 0) {
            printf("Timeout waiting for button...\n");
        }
        
        count++;
        if (count > 20) break; // Auto stop after some events
    }

    close(fd);
    return 0;
}
