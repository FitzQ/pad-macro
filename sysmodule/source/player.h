// 手柄输入播放工具接口
#pragma once
#include <switch/types.h>
#include <switch/services/hiddbg.h>
#include "util/notifled.h"

void start_play(char *path, bool *playing);
void player_set_hdls_state(const HiddbgHdlsState *state);
