/* negroni: vendor display backend logging -> kernel log (/dev/kmsg), so it shows up on the USB serial log.
 * Formats are hilog style: "%{public}s" / "%{private}d" lose the braces before printf sees them. */
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

static inline void NegroniKlog(const char *level, const char *fmt, ...)
{
    static int fd = -2;
    char clean[256], buf[512];
    size_t j = 0;
    va_list ap;

    for (size_t i = 0; fmt[i] && j < sizeof(clean) - 1; i++) {
        if (fmt[i] == '%' && fmt[i + 1] == '{') {
            const char *close = strchr(fmt + i, '}');
            clean[j++] = '%';
            if (close) {
                i = close - fmt;
                continue;
            }
        }
        clean[j++] = fmt[i];
    }
    clean[j] = 0;
    if (fd == -2)
        fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    int n = snprintf(buf, sizeof(buf), "%sNEGRONI-DISP: ", level);
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n, clean, ap);
    va_end(ap);
    if (n >= (int)sizeof(buf))
        n = sizeof(buf) - 1;
    if (fd >= 0)
        write(fd, buf, n);
}
