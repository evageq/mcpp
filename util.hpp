#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

extern bool SHELL_DEBUG;
#define DEBUG 1

#define SHELL(s, ...)                               \
    ({                                              \
        char cmd[BUFCMDSZ];                         \
        snprintf(cmd, LENGTH(cmd), s, __VA_ARGS__); \
        if (SHELL_DEBUG == true)                    \
        {                                           \
            debug("exec: %s", cmd);                 \
        }                                           \
        system(cmd);                                \
    })

#define error(...) _error(__FILE__, __LINE__, __VA_ARGS__)
#define debug(...) _debug(__FILE__, __LINE__, __VA_ARGS__)

void _error(const char *f, int line, const char *fmt, ...);
void _debug(const char *f, int line, const char *fmt, ...);
void print_hex_packet(size_t n, const uint8_t *buf, int bytes_read);
void SetNonBlocking(int fd);

#endif // __UTIL_H__
