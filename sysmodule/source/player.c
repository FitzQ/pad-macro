// Replays recorded 24-byte input frames; injection is provided by caller via callback.
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util/log.h"
#include <switch/services/hiddbg.h>
#include "common.h"
#include "controller.h"
#include "player.h"

static Thread g_playThread;         // 线程
static alignas(0x1000) u8 playThreadStack[0x4000]; // 16KB 栈
static bool g_playing = false;     // 正在播放
static Mutex g_playerMutex;        // 互斥

// Each frame: buttons(u64 LE), l.x(s32 LE), l.y(s32 LE), r.x(s32 LE), r.y(s32 LE)
typedef struct {
	u64 buttons;
	s32 lx, ly;
	s32 rx, ry;
} Frame24;

static Result frame24_to_hdls_state(const Frame24 *fr, HiddbgHdlsState *state) {
    if (!fr || !state) return -1;
    state->buttons = fr->buttons;
    state->analog_stick_l.x = fr->lx; state->analog_stick_l.y = fr->ly;
    state->analog_stick_r.x = fr->rx; state->analog_stick_r.y = fr->ry;
    return 0;
}

static bool read_frame24(FILE *f, Frame24 *out) {
	u8 buf[24];
	size_t n = fread(buf, 1, sizeof(buf), f);
	if (n != sizeof(buf)) return false;
	// Little-endian decode
	u64 b = 0;
	for (int i=0;i<8;++i) { b |= ((u64)buf[i]) << (8*i); }
	out->buttons = b;
	// helper
	#define RD_S32_LE(p) ( (s32)((u32)((p)[0]) | ((u32)(p)[1]<<8) | ((u32)(p)[2]<<16) | ((u32)(p)[3]<<24)) )
	out->lx = RD_S32_LE(&buf[8]);
	out->ly = RD_S32_LE(&buf[12]);
	out->rx = RD_S32_LE(&buf[16]);
	out->ry = RD_S32_LE(&buf[20]);
	#undef RD_S32_LE
	return true;
}

// Playback with injection callback; apply(frame,user) returns true to continue, false to stop.
bool player_play_file_with(const char *path, void (*apply)(const Frame24*, const Frame24*)) {
	FILE *f = fopen(path, "rb");
	if (!f) { log_error("[player] open failed: %s", path); return false; }
	log_info("[player] start replay: %s", path);
	u64 frames = 0;
	bool ok = true;
	while (g_playing) {
		Frame24 l = {0}, r = {0};
		if (!read_frame24(f, &l)) break;
		if (!read_frame24(f, &r)) break;
        apply(&l, &r);
		frames++;
		// svcSleepThread(16 * 1000000ULL); // ~60Hz default
		svcSleepThread(5 * 1000000ULL); // ~200Hz default
	}
	fclose(f);
	log_info("[player] replay done, frames=%llu", frames);
	return ok;
}

// --- Concrete playback using HDLS Rail-merge ---
static void player_apply_hdls(const Frame24 *l, const Frame24 *r) {
    HiddbgHdlsState lhdlsState = {0}, rhdlsState = {0};
    frame24_to_hdls_state(l, &lhdlsState);
    frame24_to_hdls_state(r, &rhdlsState);
    writeState(&lhdlsState, &rhdlsState);
}

static void playThreadFun(void *arg) {
    const char *path = (const char*)arg;
    setLedPattern(LED_PATTERN_BREATHING);
    mutexLock(&g_playerMutex); g_playing = true; mutexUnlock(&g_playerMutex);
    player_play_file_with(path, player_apply_hdls);
	mutexLock(&g_playerMutex); g_playing = false; mutexUnlock(&g_playerMutex);
    setLedPattern(LED_PATTERN_OFF);
}

static void stop_playing() {
	mutexLock(&g_playerMutex);
	g_playing = false;
    if (g_playThread.handle) {
        threadWaitForExit(&g_playThread);
        threadClose(&g_playThread);
        g_playThread.handle = 0;
    }
	mutexUnlock(&g_playerMutex);
    log_info("[player] stop");
}
void start_play(char *path) {
	mutexLock(&g_playerMutex);
    if (g_playing) {
		mutexUnlock(&g_playerMutex);
		log_warning("[player] already playing; stop first");
		return;
	}
	if (g_playThread.handle) {
        threadClose(&g_playThread);
        g_playThread.handle = 0;
	}
	mutexUnlock(&g_playerMutex);
    Result r = threadCreate(&g_playThread, playThreadFun, path, playThreadStack, sizeof(playThreadStack), 49, -2);
    if (R_FAILED(r)) {
        log_error("[player] poll thread create failed %x", r);
        return;
    }
    if (R_FAILED(threadStart(&g_playThread))) {
        log_error("[player] poll thread start failed");
        threadClose(&g_playThread);
        return;
    }
    log_info("[player] start");
}

bool player_is_playing() {
	return g_playing;
}