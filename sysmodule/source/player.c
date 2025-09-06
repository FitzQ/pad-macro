// Replays recorded 24-byte input frames; injection is provided by caller via callback.
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <log.h>
#include <time.h>
#include <switch/services/hiddbg.h>
#include "common.h"
#include "controller.h"
#include "player.h"
#include <HdlsStatePairQueue.h>

static Thread g_playThreadRead;
static Thread g_playThreadWrite;
static alignas(0x1000) u8 playThreadReadStack[0x2000]; // 8KB 栈
static alignas(0x1000) u8 playThreadWriteStack[0x2000]; // 8KB 栈
static volatile bool g_playing = false;     // 正在播放
static Mutex g_playerMutex;        // 互斥
static HdlsStatePairQueue g_hdlsStatePairQueue;


static bool read_state(FILE *f, HdlsStatePair *out) {
	size_t n = fread(out, 1, sizeof(HdlsStatePair), f);
	if (n != sizeof(HdlsStatePair)) return false;
	return true;
}

// Playback with injection callback; apply(frame,user) returns true to continue, false to stop.
bool player_play_file_with(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) { log_error("[player] open failed: %s", path); return false; }
	log_info("[player] start replay: %s", path);
	bool ok = true;
	while (g_playing) {
		HdlsStatePair statePair = {0};
		if (!read_state(f, &statePair)) break;
        hdlsStatePairQueueEnqueue(&g_hdlsStatePairQueue, statePair);
	}
	fclose(f);
	return ok;
}

static void playThreadRead(void *arg) {
    hdlsStatePairQueueInit(&g_hdlsStatePairQueue);
    const char *path = (const char*)arg;
    setLedPattern(LED_PATTERN_BREATHING);
    player_play_file_with(path);
	log_info("player reader done");
	g_playing = false;
    threadExit();
}

static void playThreadWrite(void *arg) {
    hdlsStatePairQueueInit(&g_hdlsStatePairQueue);
	HdlsStatePair statePair;
    u64 startTime = 0, endTime = 0;
    long frames = 0;
    timeGetCurrentTime(TimeType_LocalSystemClock, &startTime);
	while (g_playing) {
        if (!hdlsStatePairQueueTryDequeue(&g_hdlsStatePairQueue, &statePair)) {
            svcSleepThread(getPadConfig().playerFPS);
            continue;
        }
		do {
			frames++;
			writeState(&statePair.left, &statePair.right);
			svcSleepThread(getPadConfig().playerFPS);
		} while (--statePair.count);
	}
	log_debug("player write accept playing stop");
    while (hdlsStatePairQueueTryDequeue(&g_hdlsStatePairQueue, &statePair))
    {
		do {
			frames++;
			writeState(&statePair.left, &statePair.right);
			svcSleepThread(getPadConfig().playerFPS);
		} while (--statePair.count);
    }
    threadWaitForExit(&g_playThreadRead);
    while (hdlsStatePairQueueTryDequeue(&g_hdlsStatePairQueue, &statePair))
    {
		do {
			frames++;
			writeState(&statePair.left, &statePair.right);
			svcSleepThread(getPadConfig().playerFPS);
		} while (--statePair.count);
    }
    timeGetCurrentTime(TimeType_LocalSystemClock, &endTime);
    log_debug("recording read finished. frames: %ld, duration: %llu ms", frames, (endTime - startTime));
    setLedPattern(LED_PATTERN_OFF);
}

static void stop_playing() {
	mutexLock(&g_playerMutex);
	g_playing = false;
    if (g_playThreadRead.handle) {
        threadWaitForExit(&g_playThreadRead);
        threadClose(&g_playThreadRead);
        g_playThreadRead.handle = 0;
    }
	if (g_playThreadWrite.handle) {
		threadWaitForExit(&g_playThreadWrite);
		threadClose(&g_playThreadWrite);
		g_playThreadWrite.handle = 0;
	}
	mutexUnlock(&g_playerMutex);
    log_info("player finished");
}
void start_play(char *path) {
	mutexLock(&g_playerMutex);
    if (g_playing) {
		mutexUnlock(&g_playerMutex);
		log_warning("[player] already playing; stop first");
		return;
	}
	if (g_playThreadRead.handle || g_playThreadWrite.handle) {
        threadClose(&g_playThreadRead);
		threadClose(&g_playThreadWrite);
        g_playThreadRead.handle = 0;
		g_playThreadWrite.handle = 0;
	}
    g_playing = true;
	mutexUnlock(&g_playerMutex);
    Result rr = threadCreate(&g_playThreadRead, playThreadRead, path, playThreadReadStack, sizeof(playThreadReadStack), 49, -2);
    Result rw = threadCreate(&g_playThreadWrite, playThreadWrite, path, playThreadWriteStack, sizeof(playThreadWriteStack), 49, -2);
    if (R_FAILED(rr) || R_FAILED(rw)) {
        log_error("[player] poll thread create failed %x %x", rr, rw);
        return;
    }
    if (R_FAILED(threadStart(&g_playThreadRead)) || R_FAILED(threadStart(&g_playThreadWrite))) {
        log_error("[player] poll thread start failed");
        threadClose(&g_playThreadRead);
        threadClose(&g_playThreadWrite);
        return;
    }
    log_info("[player] start");
}

bool player_is_playing() {
	return g_playing;
}