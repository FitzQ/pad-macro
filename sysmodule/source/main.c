// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/log.h"
#include "common.h"
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

// Include the main libnx system header, for Switch development
#include <switch.h>
#include <switch/services/sfdnsres.h>

#define R_ASSERT(res_expr)            \
    ({                                \
        const Result rc = (res_expr); \
        if (R_FAILED(rc))             \
        {                             \
            fatalThrow(rc);           \
        }                             \
    })

// Size of the inner heap (adjust as necessary).
#define INNER_HEAP_SIZE 0x200000 // 2MB heap size

#ifdef __cplusplus
extern "C"
{
#endif

    // Sysmodules should not use applet*.
    u32 __nx_applet_type = AppletType_None;

    // Sysmodules will normally only want to use one FS session.
    // Increase FS sessions to avoid potential blocking when logger and other libraries
    // (e.g., curl) both access FS concurrently.
    u32 __nx_fs_num_sessions = 3;

    // Newlib heap configuration function (makes malloc/free work).
    void __libnx_initheap(void)
    {
        static u8 inner_heap[INNER_HEAP_SIZE];
        extern void *fake_heap_start;
        extern void *fake_heap_end;

        // Configure the newlib heap.
        fake_heap_start = inner_heap;
        fake_heap_end = inner_heap + sizeof(inner_heap);
    }

    // Service initialization.
    void __appInit(void)
    {
        Result rc;

        // Open a service manager session.
        R_ASSERT(smInitialize());

        // Retrieve the current version of Horizon OS.
        rc = setsysInitialize();
        if (R_SUCCEEDED(rc))
        {
            SetSysFirmwareVersion fw;
            rc = setsysGetFirmwareVersion(&fw);
            if (R_SUCCEEDED(rc))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
            setsysExit();
        }

        // Enable this if you want to use HID.
        R_ASSERT(hidInitialize());
        R_ASSERT(hiddbgInitialize());

        R_ASSERT(hidsysInitialize());

        // Enable this if you want to use time.
        R_ASSERT(timeInitialize());

        R_ASSERT(notifInitialize(NotifServiceType_System));
        // Disable this if you don't want to use the filesystem.
        R_ASSERT(fsInitialize());

        // Disable this if you don't want to use the SD card filesystem.
        R_ASSERT(fsdevMountSdmc());

        // Initialize sockets before starting our module so early logs can be sent over UDP.
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
        socketInitialize(&socketInitConfig);

        // Add other services you want to use here.

        // Close the service manager session.
        smExit();
    }

    // Service deinitialization.
    void __appExit(void)
    {
        log_info("__appExit");
        // Close extra services you added to __appInit here.
        notifExit();
        log_info("notifExited");
        hidsysExit();     // Enable this if you want to use hidsys.
        log_info("hidsysExited");
        hiddbgExit();      // Debug HID service
        log_info("hiddbgExited");
        hidExit(); // Enable this if you want to use HID.
        log_info("hidExited");
        socketExit();
        fsdevUnmountAll(); // Disable this if you don't want to use the SD card filesystem.
        fsExit();          // Disable this if you don't want to use the filesystem.
        timeExit();        // Enable this if you want to use time.
    }

#ifdef __cplusplus
}
#endif

// Main program entrypoint
int main(int argc, char *argv[])
{
    log_info("end loop");
    R_ASSERT(padMacroInitialize());
    while (true)
    {
        svcSleepThread(1e+8L);
    }
    log_info("end loop");
    padMacroFinalize();
    log_info("padMacroExited");
    return 0;
}