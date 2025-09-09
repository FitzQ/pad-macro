#include "common.h"
#include "controller.h"
#define LATEST_MACRO_PATH "sdmc:/config/pad-macro/macros/latest.bin"

static Thread listen_thread;         // 监听线程
static alignas(0x1000) u8 listen_thread_stack[0x4000]; // 16KB 栈
static volatile bool exiting = true;
// 监听线程
static void listenThreadFun(void *arg) {
    (void)arg;
    log_info("listen thread started");
    while (exiting) {
        HiddbgHdlsState l = {0}, r = {0};
        Result rc = readState(&l, &r);
        char *macro_path = NULL;
        if (R_SUCCEEDED(rc) && (l.buttons != 0 || r.buttons != 0)) {
            if (playerEnable() && ((l.buttons | r.buttons) & getPlayLatestButtonMask()) == getPlayLatestButtonMask() && !recorder_is_recording() && !player_is_playing()) {
                log_info("macro matched: latest");
                // 执行宏
                start_play(LATEST_MACRO_PATH);
                // 间隔 1000ms
                svcSleepThread(1000 * 1000000ULL);
            } else if (playerEnable() && maskMatch(l.buttons|r.buttons, &macro_path) && macro_path != NULL && macro_path[0] != '\0' && !recorder_is_recording() && !player_is_playing()) {
                log_info("macro matched: %s", macro_path);
                // 执行宏
                start_play(macro_path);
                // 间隔 1000ms
                svcSleepThread(1000 * 1000000ULL);
            } else if (recorderEnable() && ((l.buttons | r.buttons) & getRecordButtonMask()) == getRecordButtonMask() && !player_is_playing()) {
                log_info("toggling recording");
                // 切换录制状态
                recorder_switch();
                // 间隔 1000ms
                svcSleepThread(1000 * 1000000ULL);
            }
        }
        // 每帧间隔 16ms
        svcSleepThread(16 * 1000000ULL);
        // svcSleepThread(5 * 1000000ULL);
    }
    threadExit();
}

Result padMacroInitialize() {
    // 加载配置
    loadConfig();
    // 加载配置
    controllerInitialize();
    // 初始化播放器
    Result rc = threadCreate(&listen_thread, listenThreadFun, NULL, listen_thread_stack, sizeof(listen_thread_stack), 49, -2);
    if (R_FAILED(rc)) {
        log_error("listen thread create failed %x", rc);
        return rc;
    }
    rc = threadStart(&listen_thread);
    if (R_FAILED(rc)) {
        log_error("listen thread start failed %x", rc);
        threadClose(&listen_thread);
        return rc;
    }
    log_info("listen thread started successfully");
    return 0;
}


void padMacroExit() {
    log_info("pad macro exit");
    exiting = false;
    freeConfig();
    controllerExit();
    if (listen_thread.handle) {
        threadWaitForExit(&listen_thread);
        threadClose(&listen_thread);
        listen_thread.handle = 0;
    }
    // close led
    setLedPattern(LED_PATTERN_OFF);
    log_info("pad macro exited");
}