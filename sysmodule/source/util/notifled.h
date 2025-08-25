#pragma once

typedef enum {
    LED_PATTERN_SOLID,
    LED_PATTERN_BLINKING,
    LED_PATTERN_BREATHING,
    LED_PATTERN_OFF
} LedPatternType;

void setLedPattern(LedPatternType type);