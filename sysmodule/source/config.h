#pragma once
#include <switch.h>
#include <stdbool.h>

// Load config from sdmc:/config/pad-macro/config.ini
bool loadConfig(void);
void freeConfig(void);

// [pad] accessors
u64 getRecordButtonMask(void);
u64 getPlayLatestButtonMask(void);
bool recorderEnable(void);
bool playerEnable(void);

// [macros] matcher
// Returns true if a mapping matches: (pressed & cfgMask) == cfgMask.
// On match, *out_path points to internal storage valid until next load().
bool maskMatch(u64 pressed, char **out_path);
