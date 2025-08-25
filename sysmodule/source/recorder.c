#include "recorder.h"
#include "controller.h"
#include <time.h>
#include <switch.h>
#include <switch/services/hid.h>
#include <switch/runtime/devices/console.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // for snprintf
#include "util/log.h"
#include "util/notifled.h"
#include "common.h"

#define LATEST_REC_FILE_PATH "sdmc:/switch/pad-macro/macros/latest.bin"
static bool g_recording = false;     // 正在录制
static Mutex g_recorderMutex;        // 互斥
static Thread g_recordThread;         // 手柄采集线程
static alignas(0x1000) u8 recordThreadStack[0x4000]; // 64KB 栈

#define RECORDER_FRAME_SIZE 24
#define LATEST_FILE_PATH "sdmc:/switch/pad-macro/macros/latest.bin"
#define CURRENT_FILE_PATH "sdmc:/switch/pad-macro/macros/%s"
static FILE *g_recordFile = NULL;

// function declarations
static void start_recording();
static void stop_recording();
static char *cur_time_filename(void);
static int copy_file(const char *src, const char *dst);

// 切换录制状态
void recorder_switch(bool *recording) {
    recording = &g_recording;
    if (g_recording) stop_recording();
    else start_recording();
}

static void recordThreadFun(void *arg) {
    (void)arg;
    g_recordFile = fopen(LATEST_FILE_PATH, "wb");
    if (!g_recordFile) {
        log_error("[recorder] fopen failed: %s", LATEST_FILE_PATH);
        threadExit();
        return;
    }
    setLedPattern(LED_PATTERN_SOLID);
    HiddbgHdlsState l = {0}, r = {0};
    u8 framebuf[RECORDER_FRAME_SIZE];
    while (g_recording) {
        Result rc = readState(&l, &r);
        if (R_SUCCEEDED(rc)) {
            if (((l.buttons|r.buttons) & getRecordButtonMask()) == getRecordButtonMask()) continue;
            // 写入左帧
            memset(framebuf, 0, RECORDER_FRAME_SIZE);
            memcpy(framebuf, &l.buttons, 8);
            memcpy(framebuf+8, &l.analog_stick_l.x, 4);
            memcpy(framebuf+12, &l.analog_stick_l.y, 4);
            memcpy(framebuf+16, &l.analog_stick_r.x, 4);
            memcpy(framebuf+20, &l.analog_stick_r.y, 4);
            fwrite(framebuf, 1, RECORDER_FRAME_SIZE, g_recordFile);
            // 写入右帧
            memset(framebuf, 0, RECORDER_FRAME_SIZE);
            memcpy(framebuf, &r.buttons, 8);
            memcpy(framebuf+8, &r.analog_stick_l.x, 4);
            memcpy(framebuf+12, &r.analog_stick_l.y, 4);
            memcpy(framebuf+16, &r.analog_stick_r.x, 4);
            memcpy(framebuf+20, &r.analog_stick_r.y, 4);
            fwrite(framebuf, 1, RECORDER_FRAME_SIZE, g_recordFile);
        }
        svcSleepThread(5 * 1000000ULL); // ~200Hz 采样
    }
    // 结束标记
    memset(framebuf, 0, RECORDER_FRAME_SIZE);
    fwrite(framebuf, 1, RECORDER_FRAME_SIZE, g_recordFile);
    fwrite(framebuf, 1, RECORDER_FRAME_SIZE, g_recordFile);
    fclose(g_recordFile); g_recordFile = NULL;
    setLedPattern(LED_PATTERN_OFF);
    char fullpath[128];
    snprintf(fullpath, sizeof(fullpath), CURRENT_FILE_PATH, cur_time_filename());
    copy_file(LATEST_FILE_PATH, fullpath);
    log_info("recording finished");
    threadExit();
}

static void start_recording() {
    mutexLock(&g_recorderMutex);
    if (g_recording) {
        mutexUnlock(&g_recorderMutex);
        log_warning("[recorder] already recording; stop first");
        return;
    }
    g_recording = true;
    mutexUnlock(&g_recorderMutex);
    Result r = threadCreate(&g_recordThread, recordThreadFun, NULL, recordThreadStack, sizeof(recordThreadStack), 49, -2);
    if (R_FAILED(r)) {
        log_error("[recorder] poll thread create failed %x", r);
        mutexLock(&g_recorderMutex); g_recording = false; mutexUnlock(&g_recorderMutex);
        return;
    }
    if (R_FAILED(threadStart(&g_recordThread))) {
        log_error("[recorder] poll thread start failed");
        threadClose(&g_recordThread);
        mutexLock(&g_recorderMutex); g_recording = false; mutexUnlock(&g_recorderMutex);
        return;
    }
    log_info("[recorder] start");
}

static void stop_recording() {
    mutexLock(&g_recorderMutex);
    g_recording = false;
    mutexUnlock(&g_recorderMutex);
    if (g_recordThread.handle) {
        threadWaitForExit(&g_recordThread);
        threadClose(&g_recordThread);
        g_recordThread.handle = 0;
    }
    log_info("[recorder] stop");
}

static char *cur_time_filename() {
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
    // Format time as "YYYY-MM-DD_HH:MM:SS.bin"
    static char timebuf[64];
    snprintf(timebuf, sizeof(timebuf), "%04i-%02i-%02i_%02i:%02i:%02i.bin",
             year, month + 1, day, hours, minutes, seconds);
    return timebuf;
}

static int copy_file(const char *src, const char *dst) {
    FILE *fin = fopen(src, "rb");
    if (!fin) return -1;
    FILE *fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); return -2; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) { fclose(fin); fclose(fout); return -3; }
    }
    fclose(fin);
    fclose(fout);
    return 0;
}