
/*
 * Build-time switch to enable/disable debug logging (file + udp socket).
 * Default: disabled to save memory in small sysmodule builds.
 * To enable at make time: pass a CFLAGS definition, for example in MSYS shell:
 *   make CFLAGS+=" -DENABLE_FULL_LOG=1"
 */
#ifndef __DEBUG__
#define __DEBUG__ 0
#endif

#if __DEBUG__

#include <stdio.h>
#include "log.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <switch/services/time.h>
#include <time.h>
#include <switch.h>
// BSD sockets for optional UDP net logging
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <switch/runtime/devices/socket.h>


static Mutex log_mutex = 0;
static bool  log_mutex_inited = false;
#define LOG_FILE_PATH "sdmc:/atmosphere/logs/pad-macro.log"
static FILE *log_file = NULL;

static bool  netlog_enabled    = false;
static int   netlog_sock       = -1;
static struct sockaddr_in netlog_addr;

#ifdef __cplusplus
extern "C"
{
#endif
static void netlog_try_init(void) {
    if (netlog_enabled) return; // already ready

    // Read config
    const char* ip = "192.168.1.3";
    int port = 10555;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        // BSD sockets likely not initialized yet; try again on next log
        return;
    }

    memset(&netlog_addr, 0, sizeof(netlog_addr));
    netlog_addr.sin_family = AF_INET;
    netlog_addr.sin_port   = htons((uint16_t)port);
    netlog_addr.sin_addr.s_addr = inet_addr(ip);
    if (netlog_addr.sin_addr.s_addr == 0xFFFFFFFFu) { // invalid address
        close(sock);
        return;
    }

    netlog_sock = sock;
    netlog_enabled = true;
}

static void netlog_send(const char *msg) {
    if (!netlog_enabled) return;
    if (netlog_sock < 0) return;
    if (!msg) return;
    sendto(netlog_sock, msg, (int)strlen(msg), 0, (struct sockaddr*)&netlog_addr, sizeof(netlog_addr));
}

static char *cur_time() {
    u64 timestamp = 0;
    timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp);
    // Convert to UTC+8 by adding 8*3600 seconds
    time_t t = (time_t)timestamp + 8 * 3600;
    struct tm *tm_ptr = gmtime(&t);
    int hours = tm_ptr->tm_hour;
    int minutes = tm_ptr->tm_min;
    int seconds = tm_ptr->tm_sec;
    int day = tm_ptr->tm_mday;
    int month = tm_ptr->tm_mon;
    int year = tm_ptr->tm_year + 1900;
    // Format time as "YYYY-MM-DD HH:MM:SS"
    static char timebuf[64];
    snprintf(timebuf, sizeof(timebuf), "%04i-%02i-%02i %02i:%02i:%02i",
             year, month + 1, day, hours, minutes, seconds);
    return timebuf;
}

static void log_write(const char *level, const char *file, int line, const char *fmt, va_list args) {
    // return; // --- IGNORE ---
    if (!log_mutex_inited) { mutexInit(&log_mutex); log_mutex_inited = true; }
    mutexLock(&log_mutex);
    netlog_try_init();
    if (!log_file) {
        log_file = fopen(LOG_FILE_PATH, "a");
        // If file open fails, continue with netlog only.
    }
    // 只打印file名最后20个字符
    const char *short_file = file;
    size_t file_len = strlen(file);
    if (file_len > 20) {
        short_file = file + file_len - 20;
    }
    // Format once into a buffer for both sinks
    char linebuf[1024];
    int hdr = snprintf(linebuf, sizeof(linebuf), "%s [%s:%d] [%s] ", cur_time(), short_file, line, level);
    int msg = vsnprintf(linebuf + (hdr>0?hdr:0), (hdr>=0? (int)sizeof(linebuf)-hdr : 0), fmt, args);
    size_t total = (hdr>0?hdr:0) + (msg>0?msg:0);
    if (total < sizeof(linebuf)-2) { linebuf[total++]= '\n'; linebuf[total]=0; }

    if (log_file) {
        fwrite(linebuf, 1, total, log_file);
        fflush(log_file);
    }
    netlog_send(linebuf);
    mutexUnlock(&log_mutex);
}



void log_info_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("INFO", file, line, fmt, args);
    va_end(args);
}

void log_warning_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("WARNING", file, line, fmt, args);
    va_end(args);
}

void log_error_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", file, line, fmt, args);
    va_end(args);
}

void log_debug_impl(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("DEBUG", file, line, fmt, args);
    va_end(args);
}
#ifdef __cplusplus
}
#endif

#else /* __DEBUG__ == 0 */

/* Minimal no-op logging implementation to avoid pulling in socket/stdio/etc when disabled.
 * Keep the function signatures so callers don't need to change. */
#include "log.h"
#include <stdarg.h>

void log_info_impl(const char *file, int line, const char *fmt, ...) {
    (void)file; (void)line; (void)fmt;
}
void log_warning_impl(const char *file, int line, const char *fmt, ...) {
    (void)file; (void)line; (void)fmt;
}
void log_error_impl(const char *file, int line, const char *fmt, ...) {
    (void)file; (void)line; (void)fmt;
}
void log_debug_impl(const char *file, int line, const char *fmt, ...) {
    (void)file; (void)line; (void)fmt;
}

#endif /* __DEBUG__ */