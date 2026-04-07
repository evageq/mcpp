#include "util.hpp"
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void
_error(const char *f, int line, const char *fmt, ...)
{
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 4096, fmt, args);
    va_end(args);
    fprintf(stderr, "[%s:%d][ERROR]: %s: %s\n", f, line, buf, strerror(errno));
}

void
_debug(const char *f, int line, const char *fmt, ...)
{
    if (DEBUG)
    {
        char buf[4096];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, 4096, fmt, args);
        va_end(args);
        fprintf(stderr, "[%s:%d][DEBUG]: %s\n", f, line, buf);
    }
}

void
print_hex_packet(size_t n, const uint8_t *buf, int bytes_read)
{
    printf("Packet:\n");
    for (int i = 0; i < bytes_read; ++i)
    {
        if (i % 8 == 0)
        {
            printf("\n\t");
        }
        printf("%02x ", buf[i]);
    }
    printf("\n");
    fflush(stdout);
}

void
SetNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    assert(flags != -1);
    const int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    assert(ret != -1);
}
