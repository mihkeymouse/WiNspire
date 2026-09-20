/* Internal release policy. Keep guest-specific choices in runtime profiles.
 * Both release builders force-include this file before any core headers.
 */
#ifndef WINSPIRE_RELEASE_CONFIG_H
#define WINSPIRE_RELEASE_CONFIG_H

#if defined(WINSPIRE_SERVER_BUILD) == defined(WINSPIRE_NATIVE_BUILD)
#error Select exactly one WiNspire release target
#endif

#define TINY386_SPEED_BUILD 1
#define TINY386_NO_LOG 1
#define BPP 16
#define SCALE_2_1 1

#ifdef WINSPIRE_SERVER_BUILD
/* HEADLESS_DIAG also selects the Server timer/device/frontend implementation;
 * it is not a logging switch. Preserve this machine shape for NT guests.
 */
#define TINY386_HEADLESS_DIAG 1
#define CALC_PROFILE_SELECT 1
#define TINY386_FB_MIRROR 1
#define TINY386_READ_PACED_RETRACE 1
#define TINY386_PC_STEP_COUNT 8192

/* Interpreter fast paths. */
#define TINY386_ARM_FAST 1
#define REP_SLICE_ENABLED 1
#define REP_SLICE 16384
#define BULK_REP_ENABLED 1
#define WIN2K_FAST_ENABLED 1

/* Array sizes affect memory use and USB disk throughput. */
#define NETBLK_READ_CACHE_SECTORS 128
#define NETBLK_READ_CACHE_WINDOWS 16
#define NETBLK_WRITE_BUFFER_SECTORS 1024
#define NETBLK_WRITEBACK_SECTORS 2048
#define I386_ENABLE_FPU 1
#else
#define BUILD_NSPIRE 1
#endif

#endif
