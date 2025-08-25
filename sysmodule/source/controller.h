

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <switch/services/hid.h> // 包含键盘按键定义
#include <switch/services/hiddbg.h>
#include "util/log.h"
#include <switch/types.h>

// void update_hdls_state(const HiddbgHdlsState *args, bool is_long_press);
// void emit_home_button();
// void emit_capture_button();
Result controllerInitialize();
void controllerFinalize();
Result readState(HiddbgHdlsState *l, HiddbgHdlsState *r);
Result writeState(const HiddbgHdlsState *l, const HiddbgHdlsState *r);