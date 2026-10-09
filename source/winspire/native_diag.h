#ifndef WINSPIRE_NATIVE_DIAG_H
#define WINSPIRE_NATIVE_DIAG_H
#include <stdint.h>

#ifdef WINSPIRE_NATIVE_DIAGNOSTICS
#ifndef BUILD_NSPIRE
#error Native diagnostics require the Native target
#endif
typedef struct {
    uint32_t batches, instructions, batch_min, batch_max, idle_batches;
    uint32_t budget_exits, exceptions, last_exception;
    uint32_t cpu_ticks, input_ticks, video_ticks, disk_ticks, log_ticks;
    uint32_t input_polls, key_events, mouse_events, video_polls;
    uint32_t redraws, lcd_claims, full_draws, region_draws;
    uint32_t render_ticks, lcd_ticks, planar_frames, fallback_frames;
    uint32_t vga_width, vga_height, vga_bpp;
    uint32_t read_calls, read_sectors, write_calls, write_sectors, disk_errors;
} NativeDiagnostics;
extern NativeDiagnostics native_diag;
#ifndef NATIVE_DIAG_TIMER_READ
#define NATIVE_DIAG_TIMER_READ() (*(volatile uint32_t *)0x900d0024u)
#endif
static inline uint32_t native_diag_timer(void) { return NATIVE_DIAG_TIMER_READ(); }
#define NATIVE_DIAG(code) do { code; } while (0)
#else
#define NATIVE_DIAG(code) do { } while (0)
#endif
#endif
