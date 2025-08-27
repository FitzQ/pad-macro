#ifndef __DEBUG__
#define __DEBUG__ 0
#endif

// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/log.h"
#include "ipc/ipc.h"
#include "common.h"
#include "controller.h"
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

// Include the main libnx system header, for Switch development
#include <switch.h>
#include <switch/services/sfdnsres.h>

#define R_ASSERT(res_expr)({const Result rc = (res_expr);if (R_FAILED(rc)){fatalThrow(rc);}})

void* workmem = NULL;
size_t workmem_size = 0x10000;

// Size of the inner heap (adjust as necessary).

#if __DEBUG__

#define HEAP_SIZE 0x100000 // 1MB heap size

#else /* __DEBUG__ == 0 */

#define HEAP_SIZE 0x40000 // 256KB heap size

#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // Sysmodules should not use applet*.
    u32 __nx_applet_type = AppletType_None;

    // Sysmodules will normally only want to use one FS session.
    // Increase FS sessions to avoid potential blocking when logger and other libraries
    // (e.g., curl) both access FS concurrently.
    u32 __nx_fs_num_sessions = 1;
    // setup a fake heap
    char fake_heap[HEAP_SIZE];

    // Newlib heap configuration function (makes malloc/free work).
    void __libnx_initheap(void)
    {
        extern void *fake_heap_start;
        extern void *fake_heap_end;

        // Configure the newlib heap.
        fake_heap_start = fake_heap;
        fake_heap_end = fake_heap + HEAP_SIZE;
    }

    // Service initialization.
    void __appInit(void)
    {
        R_ASSERT(smInitialize());
        log_info("smInitialize success");

#if __DEBUG__

        static const SocketInitConfig socketInitConfig = {
            .tcp_tx_buf_size = 0x800,
            .tcp_rx_buf_size = 0x800,
            .tcp_tx_buf_max_size = 0x25000,
            .tcp_rx_buf_max_size = 0x25000,

            // Enable UDP for net logging
            .udp_tx_buf_size = 0x2400,
            .udp_rx_buf_size = 0x2400,

            .sb_efficiency = 1,
        };
        R_ASSERT(socketInitialize(&socketInitConfig));
        log_info("socketInitializeDefault success");

#endif

        R_ASSERT(timeInitialize());
        log_info("timeInitialize success");
        R_ASSERT(fsInitialize());
        log_info("fsInitialize success");
        R_ASSERT(fsdevMountSdmc());
        log_info("fsdevMountSdmc success");
        R_ASSERT(hidInitialize());
        log_info("hidInitialize success");
        R_ASSERT(hiddbgInitialize());
        log_info("hiddbgInitialize success");
        R_ASSERT(hidsysInitialize());
        log_info("hidsysInitialize success");
        R_ASSERT(setsysInitialize());
        log_info("setsysInitialize success");
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
        R_ASSERT(notifInitialize(NotifServiceType_System));
        log_info("notifInitialize success");
        workmem = aligned_alloc(0x1000, workmem_size);
        if (!workmem)
        {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));
        }
        log_info("aligned_alloc success");
        R_ASSERT(hiddbgAttachHdlsWorkBuffer(getHdlsSessionId(), workmem, workmem_size));
        log_info("hiddbgAttachHdlsWorkBuffer success");
    }

    // Service deinitialization.
    void __appExit(void)
    {
        log_info("__appExit 实际上在关闭这个后台服务时没有被执行");
        R_ASSERT(hiddbgReleaseHdlsWorkBuffer(*getHdlsSessionId()));
        free(workmem);
        notifExit();
        hidsysExit();
        hiddbgExit();
        hidExit();
        socketExit();
        fsdevUnmountAll();
        fsExit();
        timeExit();
        smExit();
    }

#ifdef __cplusplus
}
#endif

// Main program entrypoint
int main(int argc, char *argv[])
{
    log_info("start loop");
    R_ASSERT(padMacroInitialize());
    IpcThread();
    return 0;
}