
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
static alignas(0x1000) u8 workmem[0x4000];
static HiddbgHdlsStateList stateList = {0};

Result controllerInitialize()
{
    log_info("memory allocated for input states");

    Result workRes = hiddbgAttachHdlsWorkBuffer(&hdlsSessionId, workmem, sizeof(workmem));
    if (R_FAILED(workRes))
    {
        log_error("Failed to attach HdlsWorkBuffer: %d\n", workRes);
        return -1;
    }

    HiddbgHdlsNpadAssignment state = {0};
    memset(&state, 0, sizeof(state));
    Result res = hiddbgDumpHdlsNpadAssignmentState(hdlsSessionId, &state);
    if (R_FAILED(res))
    {
        log_error("Failed to dump HDLS Npad Assignment State: %d\n", res);
        return -2;
    }
    initialized = 1;
    log_info("Controller initialized successfully");
    return 0;
}

void controllerFinalize()
{
    if (hdlsSessionId.id)
    {
        hiddbgReleaseHdlsWorkBuffer(hdlsSessionId);
        hdlsSessionId = (HiddbgHdlsSessionId){0};
    }
    initialized = 0;
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