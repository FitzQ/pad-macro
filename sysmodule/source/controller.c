
#include <switch/types.h>
#include "controller.h"
#include <switch/services/hid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <switch/services/hid.h> // 包含键盘按键定义
#include <switch/services/hiddbg.h>

static int initialized = 0;
static HiddbgHdlsSessionId hdlsSessionId = {0};
static alignas(0x1000) u8 workmem[0x1000]; // 4KB 工作内存
static HiddbgHdlsStateList stateList = {0};


HiddbgHdlsSessionId* getHdlsSessionId()
{
    return &hdlsSessionId;
}

Result controllerInitialize()
{
    if (initialized == 1) return 1;
    Result res = hiddbgAttachHdlsWorkBuffer(&hdlsSessionId, workmem, 0x1000);
    if (R_FAILED(res))
    {
        log_error("Failed to attach HDLS work buffer: %d\n", res);
        hdlsSessionId.id = 0;
        return res;
    }
    log_info("HDLS work buffer attached successfully.\n");
    initialized = 1;
    return 0;
}

Result controllerExit()
{
    if (initialized == 0) return 0;
    Result res = hiddbgReleaseHdlsWorkBuffer(hdlsSessionId);
    if (R_FAILED(res))
    {
        log_error("Failed to release HDLS work buffer: %d\n", res);
        return res;
    }
    log_info("HDLS work buffer released successfully.\n");
    initialized = 0;
    return 0;
}

Result readState(HiddbgHdlsState *l, HiddbgHdlsState *r) {
    Result res = hiddbgDumpHdlsStates(hdlsSessionId, &stateList);
    if (R_FAILED(res)) return res;
    if (stateList.entries[0].device.deviceType == HidDeviceType_JoyLeft2) {
        *l = stateList.entries[0].state;
        *r = stateList.entries[1].state;
    } else {
        *r = stateList.entries[0].state;
        *l = stateList.entries[1].state;
    }
    return 0;
}

Result writeState(const HiddbgHdlsState *l, const HiddbgHdlsState *r) {
    
    if (stateList.entries[0].device.deviceType == HidDeviceType_JoyLeft2) {
        stateList.entries[0].state = *l;
        stateList.entries[1].state = *r;
    } else {
        stateList.entries[0].state = *r;
        stateList.entries[1].state = *l;
    }
    return hiddbgApplyHdlsStateList(hdlsSessionId, &stateList);
}