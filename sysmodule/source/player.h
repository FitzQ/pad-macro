// 手柄输入播放工具接口
#pragma once
#include <switch/types.h>
#include <switch/services/hiddbg.h>
#include "util/notifled.h"
bool player_is_playing();
void start_play(char *path);
void player_set_hdls_state(const HiddbgHdlsState *state);
