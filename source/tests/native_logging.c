#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc.h"

static uint32_t test_rtc, test_timer, test_cycle;
#define NATIVE_DIAG_RTC_READ() test_rtc
#define NATIVE_DIAG_TIMER_READ() test_timer
#define NATIVE_TIMER_READ(offset) ((offset)==8 ? 0x82u : test_timer)
#define NATIVE_TIMER_WRITE(offset,value) ((void)0)
#include "../winspire-ndless/native_clock.h"
static uint32_t guest_hz=4770000, input_poll_loops=2, video_poll_loops=1;
#define TINY386_VERSION "logging-test"
#include "../winspire-ndless/native_log.h"

long cpui386_get_cycle(CPUI386 *cpu) { (void)cpu; return (long)test_cycle; }
void cpui386_snapshot(CPUI386 *cpu, CPUI386Snapshot *state)
{
    (void)cpu;
    memset(state,0,sizeof(*state));
    state->cs=0x28;state->ip=0xc0001234;state->interrupt_enabled=1;
}

static const char *read_log(void)
{
    static char contents[8192];
    FILE *file=fopen("winspire-log.txt.tns","rb");
    assert(file);
    size_t bytes=fread(contents,1,sizeof(contents)-1,file);
    contents[bytes]=0;
    fclose(file);
    return contents;
}

int main(void)
{
    assert(native_log_open());
    PCConfig config={0};config.mem_size=16*1024*1024;config.cpu_gen=4;
    config.disks[0]="disk.img.tns";
    NativeClock clock={0};clock.load=123;clock.control=456;
    test_rtc=UINT32_MAX-1;test_timer=10;
    native_log_start(&config,&clock);
    assert(strstr(read_log(),"timer_expected_hz=32768"));
    PC pc={0};
    test_cycle=16;
    native_log_step(NULL,UINT32_MAX-15);
    native_diag.budget_exits=1;
    native_diag.read_calls=1;native_diag.read_sectors=8;
    test_rtc++;
    native_log_sample(&pc,100,0);
    assert(!strstr(read_log(),"total_ins="));
    test_rtc++;test_timer=UINT32_MAX-5;
    native_log_sample(&pc,200,0);
    const char *row=read_log();
    assert(strstr(row,"t=2 dt=2 ticks=16 guest_us=200 total_ins=32"));
    assert(strstr(row,"ins=32 batches=1 min=32 max=32 idle=0 budget=1"));
    assert(strstr(row,"reads=1 read_sectors=8"));
    assert(strstr(row,"cs:ip=0028:c0001234"));
    test_cycle=26;
    native_log_step(NULL,16);
    native_log_sample(&pc,300,1);
    row=read_log();
    assert(strstr(row,"total_ins=42 ins=10 batches=1 min=10 max=10 idle=0 budget=0"));
    native_log_close("ON key");
    assert(strstr(read_log(),"stop: ON key"));
    remove("winspire-log.txt.tns");
    puts("PASS: Native log flush, sampling, counter reset and 32-bit clock/cycle wrap");
    return 0;
}
