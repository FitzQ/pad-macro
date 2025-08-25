
#include <switch.h>
#include <string.h>
#include "notifled.h"
#include "log.h"

#define MAX_PADS 8

static HidsysNotificationLedPattern led_pattern = {0};
static HidsysUniquePadId padId = {0};

void initLedPattern(LedPatternType type) {
    memset(&led_pattern, 0, sizeof(led_pattern));
    switch (type) {
        case LED_PATTERN_SOLID:
            led_pattern.baseMiniCycleDuration = 0x0F;
            led_pattern.startIntensity = 0xF;
            led_pattern.miniCycles[0].ledIntensity = 0xF;
            led_pattern.miniCycles[0].transitionSteps = 0x0F;
            led_pattern.miniCycles[0].finalStepDuration = 0x0F;
            break;
        case LED_PATTERN_BLINKING:
            led_pattern.baseMiniCycleDuration = 0x8; // 100ms
            break;
        case LED_PATTERN_BREATHING:
            led_pattern.baseMiniCycleDuration = 0x8;
            led_pattern.totalMiniCycles = 0x1;
            led_pattern.startIntensity = 0x2;
            // --- 配置第一个Mini Cycle：渐亮 (Fade In) ---
            led_pattern.miniCycles[0].ledIntensity = 0xF;
            led_pattern.miniCycles[0].transitionSteps = 0xF;     // 15 steps to get there (smooth)
            led_pattern.miniCycles[0].finalStepDuration = 0x1;   // hold at full brightness 12.5ms

            // --- 配置第二个Mini Cycle：渐暗 (Fade Out) ---
            led_pattern.miniCycles[1].ledIntensity = 0x2;
            led_pattern.miniCycles[1].transitionSteps = 0xF;     // 15 steps to get there (smooth)
            break;
        case LED_PATTERN_OFF:
            break;
    }
}

// 找到第一个支持的手柄
void initPad() {
    initLedPattern(LED_PATTERN_OFF);
    HidNpadIdType controllerTypes[MAX_PADS] = {HidNpadIdType_Handheld,
        HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
        HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7};
    for (int i = 0; i < MAX_PADS; i++) {
        HidsysUniquePadId padIds[MAX_PADS];
        s32 total_entries = 0;

        // Retrieve UniquePadIds for each controller type
        Result rc = hidsysGetUniquePadsFromNpad(controllerTypes[i], padIds, MAX_PADS, &total_entries);

        if (R_SUCCEEDED(rc) && total_entries > 0) {
            for (int j = 0; j < total_entries; j++) {
                rc = hidsysSetNotificationLedPattern(&led_pattern, padIds[j]);
                if (R_SUCCEEDED(rc)) {
                    padId = padIds[j];
                    log_info("Found controller type %d with UniquePadId: %llu", controllerTypes[i], padId.id);
                    return; // 找到第一个支持的手柄就返回
                }
            }
        }
    }
    log_error("No supported controller found");
}

void setLedPattern(LedPatternType type) {
    initPad();
    initLedPattern(type);
    hidsysSetNotificationLedPattern(&led_pattern, padId);
}

// ------------------------ audio notification ------------------------