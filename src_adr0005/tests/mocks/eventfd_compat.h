/*
 * eventfd compatibility shim for non-Linux test hosts (e.g., macOS)
 * Production code uses real eventfd on Linux.
 */
#ifndef EVENTFD_COMPAT_H
#define EVENTFD_COMPAT_H

#ifdef __linux__
#include <sys/eventfd.h>
#else

#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>

#define EFD_NONBLOCK 1
#define EFD_CLOEXEC  2

/*
 * Emulate eventfd using pipe for testing on non-Linux hosts.
 * Returns read end of pipe; write end stored in global for eventfd_write.
 * Limitation: only supports one eventfd per process (sufficient for tests).
 */
static int _eventfd_write_end = -1;

static inline int eventfd(unsigned int initval, int flags) {
    (void)initval;
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return -1;
    }

    if (flags & EFD_NONBLOCK) {
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    }
    if (flags & EFD_CLOEXEC) {
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    }

    _eventfd_write_end = pipefd[1];
    return pipefd[0];
}

/*
 * For wakeup, write to the write end of the pipe.
 * In ubus_thread_wakeup, we write to ut->wakeup_fd which is the read end.
 * So we need to intercept and use the write end instead.
 */
static inline int eventfd_compat_wakeup(int fd) {
    (void)fd;
    if (_eventfd_write_end >= 0) {
        uint64_t val = 1;
        return write(_eventfd_write_end, &val, sizeof(val));
    }
    return -1;
}

static inline void eventfd_compat_close(int fd) {
    close(fd);
    if (_eventfd_write_end >= 0) {
        close(_eventfd_write_end);
        _eventfd_write_end = -1;
    }
}

#endif /* __linux__ */

#endif /* EVENTFD_COMPAT_H */
