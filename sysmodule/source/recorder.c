#include "recorder.h"
#include "controller.h"
#include <time.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <switch/services/hid.h>
#include <switch/runtime/devices/console.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // for snprintf
#include <log.h>
#include "util/notifled.h"
#include "common.h"
#include <errno.h>
#include <HdlsStatePairQueue.h>

static bool g_recording = false;     // 正在录制
static Mutex g_recorderMutex;        // 互斥
static Thread g_recordThreadRead;         // 手柄采集线程
static Thread g_recordThreadWrite;         // 手柄写入线程
static alignas(0x1000) u8 recordThreadReadStack[0x2000]; // 8KB 栈
static alignas(0x1000) u8 recordThreadWriteStack[0x2000]; // 8KB 栈

#define RECORDER_FRAME_SIZE 24
#define LATEST_FILE_PATH "/config/pad-macro/macros/latest.bin"
#define CURRENT_FILE_PATH "/config/pad-macro/macros/%s"
static FILE *g_recordFile = NULL;

// function declarations
static void start_recording();
static void stop_recording();
static char *cur_time_filename(void);
static int copy_file(const char *src, const char *dst);

// 切换录制状态
void recorder_switch() {
    if (g_recording) stop_recording();
    else start_recording();
}

// 创建 filepath 的父目录（递归），返回 0 成功，-1 失败并设置 errno
static int ensure_parent_dirs(const char *filepath) {
    if (!filepath) { errno = EINVAL; return -1; }
    char *buf = strdup(filepath);
    if (!buf) { errno = ENOMEM; return -1; }

    // 去掉最后的文件名部分：找到最后一个 '/'
    char *p = strrchr(buf, '/');
    if (!p) { free(buf); return 0; } // 没有目录部分，当前目录即可

    *p = '\0'; // buf 现在是父目录路径
    // 创建每一层目录（从第一个 '/' 之后开始，保留根 '/'）
    for (char *s = buf + 1; *s; ++s) {
        if (*s == '/') {
            *s = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                free(buf);
                return -1;
            }
            *s = '/';
        }
    }
    // 最后一层
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}
static HdlsStatePairQueue g_hdlsStatePairQueue;
static void recordThreadRead(void *arg) {
    (void)arg;
    hdlsStatePairQueueInit(&g_hdlsStatePairQueue);
    setLedPattern(LED_PATTERN_SOLID);
    HiddbgHdlsState cl = {0}, cr = {0}, ll = {0}, lr = {0};
    int count = 1;
    u64 startTime = 0, endTime = 0;
    long frames = 0;
    timeGetCurrentTime(TimeType_LocalSystemClock, &startTime);
    while (g_recording) {
        Result rc = readState(&cl, &cr);
        if (R_SUCCEEDED(rc)) {
            if (((cl.buttons|cr.buttons) & getRecordButtonMask()) == getRecordButtonMask()) continue;
            svcSleepThread(getPadConfig().recorderFPS);frames++;
            if (memcmp(&cl, &ll, sizeof(HiddbgHdlsState)) == 0 && memcmp(&cr, &lr, sizeof(HiddbgHdlsState)) == 0) {
                count++;
                continue;
            } else {
                HdlsStatePair pair = { .left = ll, .right = lr, .count = count };
                hdlsStatePairQueueEnqueue(&g_hdlsStatePairQueue, pair);
                count = 1;
                ll = cl; lr = cr;
            }
        }
    }
    timeGetCurrentTime(TimeType_LocalSystemClock, &endTime);
    log_debug("recording read finished. frames: %ld, duration: %llu ms", frames, (endTime - startTime));
    setLedPattern(LED_PATTERN_OFF);
    threadExit();
}

static void recordThreadWrite(void *arg) {
    (void)arg;
    if (ensure_parent_dirs(LATEST_FILE_PATH) != 0) {
        log_error("[recorder] ensure_parent_dirs failed: %s (%d)", strerror(errno), errno);
        threadExit();
        return;
    }
    char newMacroFilePath[50];
    snprintf(newMacroFilePath, sizeof(newMacroFilePath), CURRENT_FILE_PATH, cur_time_filename());
    hdlsStatePairQueueInit(&g_hdlsStatePairQueue);
    g_recordFile = fopen(LATEST_FILE_PATH, "wb");
    FILE *newMacroFile = fopen(newMacroFilePath, "wb");
    HdlsStatePair pair;
    while (g_recording)
    {
        if (!hdlsStatePairQueueTryDequeue(&g_hdlsStatePairQueue, &pair)) {
            svcSleepThread(getPadConfig().recorderFPS);
            continue;
        }
        // Write the pair to the latest Macro file
        fwrite(&pair, 1, sizeof(HdlsStatePair), g_recordFile);
        // Write the pair to the new Macro file
        fwrite(&pair, 1, sizeof(HdlsStatePair), newMacroFile);
    }
    while (hdlsStatePairQueueTryDequeue(&g_hdlsStatePairQueue, &pair))
    {
        log_debug("flushing remaining pair");
        fwrite(&pair, 1, sizeof(HdlsStatePair), g_recordFile);
        fwrite(&pair, 1, sizeof(HdlsStatePair), newMacroFile);
    }
    threadWaitForExit(&g_recordThreadRead);
    while (hdlsStatePairQueueTryDequeue(&g_hdlsStatePairQueue, &pair))
    {
        log_debug("flushing remaining pair");
        fwrite(&pair, 1, sizeof(HdlsStatePair), g_recordFile);
        fwrite(&pair, 1, sizeof(HdlsStatePair), newMacroFile);
    }
    log_debug("write empty pair at last");
    memset(&pair.left.buttons, 0, sizeof(pair.left.buttons));
    memset(&pair.right.buttons, 0, sizeof(pair.right.buttons));
    memset(&pair.left.analog_stick_l, 0, sizeof(pair.left.analog_stick_l));
    memset(&pair.right.analog_stick_r, 0, sizeof(pair.right.analog_stick_r));
    pair.count = 1;
    fwrite(&pair, 1, sizeof(HdlsStatePair), g_recordFile);
    fwrite(&pair, 1, sizeof(HdlsStatePair), newMacroFile);
    log_debug("finalizing recording");
    fclose(g_recordFile);
    fclose(newMacroFile);
    g_recordFile = NULL;
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
    Result rr = threadCreate(&g_recordThreadRead, recordThreadRead, NULL, recordThreadReadStack, sizeof(recordThreadReadStack), 49, -2);
    Result rw = threadCreate(&g_recordThreadWrite, recordThreadWrite, NULL, recordThreadWriteStack, sizeof(recordThreadWriteStack), 49, -2);
    if (R_FAILED(rr) || R_FAILED(rw)) {
        log_error("[recorder] poll thread create failed %x %x", rr, rw);
        mutexLock(&g_recorderMutex); g_recording = false; mutexUnlock(&g_recorderMutex);
        return;
    }
    if (R_FAILED(threadStart(&g_recordThreadRead)) || R_FAILED(threadStart(&g_recordThreadWrite))) {
        log_error("[recorder] poll thread start failed");
        threadClose(&g_recordThreadRead);
        threadClose(&g_recordThreadWrite);
        mutexLock(&g_recorderMutex); g_recording = false; mutexUnlock(&g_recorderMutex);
        return;
    }
    log_info("[recorder] start");
}

static void stop_recording() {
    mutexLock(&g_recorderMutex);
    g_recording = false;
    mutexUnlock(&g_recorderMutex);
    if (g_recordThreadRead.handle) {
        threadWaitForExit(&g_recordThreadRead);
        threadClose(&g_recordThreadRead);
        g_recordThreadRead.handle = 0;
    }
    if (g_recordThreadWrite.handle) {
        threadWaitForExit(&g_recordThreadWrite);
        threadClose(&g_recordThreadWrite);
        g_recordThreadWrite.handle = 0;
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
    // Format time as "YYYY-MM-DD_HH-MM-SS.bin"
    static char timebuf[64];
    snprintf(timebuf, sizeof(timebuf), "%04i-%02i-%02i_%02i-%02i-%02i.bin",
             year, month + 1, day, hours, minutes, seconds);
    return timebuf;
}

static int copy_file(const char *src, const char *dst) {
    FILE *fin = fopen(src, "rb");
    if (!fin) {
        log_error("[recorder] copy_file: fopen source failed: %s", src);
        return -1;
    }
    FILE *fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); log_error("[recorder] copy_file: fopen dest failed: %s", dst); return -2; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) {
            fclose(fin); fclose(fout);
            log_error("[recorder] copy_file: fwrite failed during copy");
            return -3;
        }
    }
    fclose(fin);
    fclose(fout);
    log_info("[recorder] copy_file success: %s -> %s", src, dst);
    return 0;
}

bool recorder_is_recording() {
    return g_recording;
}