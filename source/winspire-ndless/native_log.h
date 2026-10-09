#ifndef WINSPIRE_NATIVE_LOG_H
#define WINSPIRE_NATIVE_LOG_H
#include "native_diag.h"

#ifdef WINSPIRE_NATIVE_DIAGNOSTICS
#ifndef NATIVE_DIAG_RTC_READ
#define NATIVE_DIAG_RTC_READ() (*(volatile uint32_t *)0x90090000u)
#endif
NativeDiagnostics native_diag;
static FILE *native_log_file;
static uint32_t log_start_rtc, log_last_rtc, log_last_timer;
static uint64_t log_total_instructions;
static char native_log_buffer[4096];

static int native_log_open(void)
{
    native_log_file=fopen("winspire-log.txt.tns","w");
    if(!native_log_file) return 0;
    setvbuf(native_log_file,native_log_buffer,_IOFBF,sizeof(native_log_buffer));
    fprintf(native_log_file,"WiNspire %s Native diagnostics\n",TINY386_VERSION);
    fflush(native_log_file);
    return !ferror(native_log_file);
}

static void native_log_close(const char *reason)
{
    if(!native_log_file) return;
    fprintf(native_log_file,"stop: %s\n",reason);
    fclose(native_log_file);
    native_log_file=NULL;
}

static void native_log_start(const PCConfig *config, const NativeClock *clock)
{
    memset(&native_diag,0,sizeof(native_diag));
    native_diag.batch_min=UINT32_MAX;
    log_start_rtc=log_last_rtc=NATIVE_DIAG_RTC_READ();
    log_last_timer=native_diag_timer();
    log_total_instructions=0;
    fprintf(native_log_file,
        "ram=%ld vram=%ld gen=%d fpu=%d guest_hz=%u disk=%s\n"
        "batch=%u budget_us=%u rep_slice=%u input_every=%u video_every=%u\n"
        "timer_expected_hz=%u saved_load=%08lx saved_control=%08lx control=%08lx rtc=%lu\n"
        "Sample durations use RTC seconds; phase durations use raw timer ticks.\n",
        config->mem_size,config->vga_mem_size,config->cpu_gen,config->fpu,
        guest_hz,config->disks[0] ? config->disks[0] : "none",
        TINY386_PC_STEP_COUNT,TINY386_NSPIRE_CPU_TIME_BUDGET_US,REP_SLICE,
        input_poll_loops,video_poll_loops,NATIVE_TIMER_HZ,
        (unsigned long)clock->load,(unsigned long)clock->control,
        (unsigned long)NATIVE_TIMER_READ(8),(unsigned long)log_start_rtc);
    fflush(native_log_file);
}

static void native_log_step(CPUI386 *cpu, uint32_t before)
{
    uint32_t count=(uint32_t)cpui386_get_cycle(cpu)-before;
    native_diag.batches++;
    native_diag.instructions+=count;
    if(count<native_diag.batch_min) native_diag.batch_min=count;
    if(count>native_diag.batch_max) native_diag.batch_max=count;
    if(!count) native_diag.idle_batches++;
    log_total_instructions+=count;
}

static void native_log_sample(PC *pc, uint32_t guest_us, int force)
{
    uint32_t rtc=NATIVE_DIAG_RTC_READ(),seconds=rtc-log_last_rtc;
    if(!force && seconds<2) return;
    uint32_t timer=native_diag_timer(),ticks=log_last_timer-timer;
    CPUI386Snapshot state;
    cpui386_snapshot(pc->cpu,&state);
    NativeDiagnostics *d=&native_diag;
    fprintf(native_log_file,
        "t=%lu dt=%lu ticks=%lu guest_us=%lu total_ins=%llu "
        "ins=%lu batches=%lu min=%lu max=%lu idle=%lu budget=%lu "
        "cpu_ticks=%lu input_ticks=%lu video_ticks=%lu disk_ticks=%lu log_ticks=%lu "
        "render_ticks=%lu lcd_ticks=%lu planar=%lu fallback=%lu mode=%lux%lux%lu "
        "input=%lu keys=%lu mouse=%lu video=%lu redraw=%lu claim=%lu full=%lu region=%lu "
        "reads=%lu read_sectors=%lu writes=%lu write_sectors=%lu io_errors=%lu "
        "exceptions=%lu last_ex=%lu cs:ip=%04lx:%08lx hlt=%d if=%d cpl=%d\n",
        (unsigned long)(rtc-log_start_rtc),(unsigned long)seconds,(unsigned long)ticks,
        (unsigned long)guest_us,(unsigned long long)log_total_instructions,
        (unsigned long)d->instructions,(unsigned long)d->batches,
        (unsigned long)(d->batches ? d->batch_min : 0),(unsigned long)d->batch_max,
        (unsigned long)d->idle_batches,(unsigned long)d->budget_exits,
        (unsigned long)d->cpu_ticks,(unsigned long)d->input_ticks,(unsigned long)d->video_ticks,
        (unsigned long)d->disk_ticks,(unsigned long)d->log_ticks,
        (unsigned long)d->render_ticks,(unsigned long)d->lcd_ticks,
        (unsigned long)d->planar_frames,(unsigned long)d->fallback_frames,
        (unsigned long)d->vga_width,(unsigned long)d->vga_height,(unsigned long)d->vga_bpp,
        (unsigned long)d->input_polls,(unsigned long)d->key_events,(unsigned long)d->mouse_events,
        (unsigned long)d->video_polls,(unsigned long)d->redraws,(unsigned long)d->lcd_claims,
        (unsigned long)d->full_draws,(unsigned long)d->region_draws,
        (unsigned long)d->read_calls,(unsigned long)d->read_sectors,
        (unsigned long)d->write_calls,(unsigned long)d->write_sectors,(unsigned long)d->disk_errors,
        (unsigned long)d->exceptions,(unsigned long)d->last_exception,
        (unsigned long)state.cs,(unsigned long)state.ip,state.halt,state.interrupt_enabled,state.cpl);
    fflush(native_log_file);
    memset(d,0,sizeof(*d));
    d->batch_min=UINT32_MAX;
    d->log_ticks=timer-native_diag_timer();
    log_last_rtc=rtc;
    log_last_timer=timer;
}
#endif
#endif
