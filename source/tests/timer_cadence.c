#define main unused_calculator_main
#include "../winspire/main.c"
#undef main
#include <assert.h>
int main(void) {
    profile_apply("/configs/win9x.ini");
    reset_guest_timer();cycle_scale=12;realtime_timer=0;
    PCConfig c={0};c.mem_size=2*1024*1024;c.vga_mem_size=256*1024;
    c.cpu_gen=4;c.width=320;c.height=240;
    PC *p=pc_new(NULL,NULL,calloc(320*240,2),&c);assert(p);
    /* PIT channel 0, LSB-only mode 2: wait for its counter to change. */
    const uint8_t code[]={0xb0,0x14,0xe6,0x43,0xb0,100,0xe6,0x40,
        0xe4,0x40,0x88,0xc3,0xe4,0x40,0x38,0xd8,0x74,0xfa,0xf4};
    memcpy(p->phys_mem+0x1000,code,sizeof(code));cpui386_reset_pm(p->cpu,0x1000);
#ifdef EXPECT_LIVE
    timer_cpu=p->cpu;timer_reads_track_cpu=1;
#endif
    pc_step(p);CPUI386Snapshot s;cpui386_snapshot(p->cpu,&s);
    printf("PIT-wait-completed-in-one-batch=%d instructions=%ld ticks=%u redraw-cadence=%s\n",
        s.halt,s.cycles,get_uticks(),getenv(E_VGA));
#ifdef EXPECT_LIVE
    assert(s.halt);
    uint32_t same=get_uticks();for(int i=0;i<100;i++) assert(get_uticks()==same);
    assert(!strcmp(getenv(E_VGA),"1"));
#endif
    puts("PASS: actual frontend timer/cadence probe");
}
