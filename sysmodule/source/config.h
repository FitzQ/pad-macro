#pragma once
#include <switch.h>
#include <stdbool.h>
typedef enum {
    FPS_30     =   1000000000ULL / 30,
    FPS_60     =   1000000000ULL / 60,
    FPS_120    =   1000000000ULL / 120,
    FPS_240    =   1000000000ULL / 240
} FPSOpt;
typedef struct {
    u64 recordButtonMask;
    u64 playLatestButtonMask;
    bool recorderEnable;
    bool playerEnable;
    FPSOpt recorderFPS;
    FPSOpt playerFPS;
} PadConfig;
// Load config from sdmc:/config/pad-macro/config.ini
bool loadConfig(void);
void freeConfig(void);

// [pad] accessors
PadConfig getPadConfig(void);
u64 getRecordButtonMask(void);
u64 getPlayLatestButtonMask(void);
bool recorderEnable(void);
bool playerEnable(void);

// [macros] matcher
// Returns true if a mapping matches: (pressed & cfgMask) == cfgMask.
// On match, *out_path points to internal storage valid until next load().
bool maskMatch(u64 pressed, char **out_path);
