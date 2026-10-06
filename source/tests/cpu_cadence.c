#include "pc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
uint32_t get_uticks(void) { return 0; }
static uint32_t simulated_host_clock, simulated_host_step;
uint32_t nspire_host_uticks(void) {
    if (simulated_host_step) { simulated_host_clock+=simulated_host_step;return simulated_host_clock; }
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)t.tv_sec*1000000u+t.tv_nsec/1000u;
}
void *pcmalloc(long n) { return calloc(1,n); }
void *bigmalloc(size_t n) { return calloc(1,n); }
void nspire_log(const char *fmt,...) { (void)fmt; }
void nspire_headless_milestone(int n) { (void)n; }
int load_rom(void *a,const char *b,uword c,int d) { abort(); }
static PC *machine(void) {
    PCConfig c={0}; c.mem_size=2*1024*1024;c.vga_mem_size=256*1024;
    c.width=320;c.height=240;c.cpu_gen=4;
    return pc_new(NULL,NULL,calloc(320*240,2),&c);
}
static void run_string(PC *p,const uint8_t *code,size_t size,unsigned count,
                       unsigned src,unsigned dst,int backward) {
    memcpy(p->phys_mem+0x1000,code,size);
    cpui386_reset_pm(p->cpu,0x1000);
    cpui386_set_gpr(p->cpu,1,count);
    cpui386_set_gpr(p->cpu,6,src);
    cpui386_set_gpr(p->cpu,7,dst);
    if(backward) cpu_setflags(p->cpu,0x400,0);
    CPUI386Snapshot s={0};unsigned calls=0;
    do {
        unsigned before=count;
        pc_step(p);cpui386_snapshot(p->cpu,&s);count=s.gpr[1];calls++;
#ifdef EXPECT_YIELD
        assert(before-count<=REP_SLICE);
#endif
        assert(calls<10000);
    } while(!s.halt);
    printf("string: opcode=%02x/%02x calls=%u remaining=%u\n",code[0],code[1],calls,count);
}
int main(void) {
    setenv("TINY386_BULK_REP_RAM","0",1);setenv("TINY386_BULK_REP_STOS","0",1);
    PC *p=machine();assert(p);
    const uint8_t busy[]={0x90,0xeb,0xfd};memcpy(p->phys_mem+0x1000,busy,sizeof(busy));
    cpui386_reset_pm(p->cpu,0x1000);long before=cpui386_get_cycle(p->cpu);
    pc_step(p);printf("default-batch-instructions=%ld\n",cpui386_get_cycle(p->cpu)-before);
#ifdef EXPECT_BATCH
    assert(cpui386_get_cycle(p->cpu)-before==EXPECT_BATCH);
#endif
#ifdef TINY386_NSPIRE_CPU_TIME_BUDGET_US
    simulated_host_step=5000;
    cpui386_reset_pm(p->cpu,0x1000);before=cpui386_get_cycle(p->cpu);
    pc_step(p);
    long bounded=cpui386_get_cycle(p->cpu)-before;
    assert(bounded>0 && bounded<1024);
    printf("host-deadline-yield-instructions=%ld\n",bounded);
    simulated_host_step=0;
#endif
    for(unsigned i=0;i<20000;i++) p->phys_mem[0x20000+i]=(uint8_t)(i*17+3);
    const uint8_t movs[]={0xf3,0xa4,0xf4};
    run_string(p,movs,sizeof(movs),20000,0x20000,0x50000,0);
    assert(!memcmp(p->phys_mem+0x20000,p->phys_mem+0x50000,20000));
    memset(p->phys_mem+0x50000,0,20000);
    run_string(p,movs,sizeof(movs),20000,0x20000+19999,0x50000+19999,1);
    assert(!memcmp(p->phys_mem+0x20000,p->phys_mem+0x50000,20000));
    const uint8_t stos[]={0xf3,0xaa,0xf4};
    run_string(p,stos,sizeof(stos),20000,0,0x50000,0);
    for(unsigned i=0;i<20000;i++) assert(p->phys_mem[0x50000+i]==0);
    /* The early stop of a comparison beyond a slice must preserve flags/CX. */
    memcpy(p->phys_mem+0x50000,p->phys_mem+0x20000,20000);p->phys_mem[0x50000+10000]^=1;
    const uint8_t cmps[]={0xf3,0xa6,0xf4};
    run_string(p,cmps,sizeof(cmps),20000,0x20000,0x50000,0);
    CPUI386Snapshot s;cpui386_snapshot(p->cpu,&s);assert(s.gpr[1]==9999);
    assert(!(cpu_getflags(p->cpu)&0x40));
    /* 16-bit address/count decoding uses the same yielding path. */
    memcpy(p->phys_mem+0x3000,p->phys_mem+0x20000,12000);
    const uint8_t addr16[]={0x67,0xf3,0xa4,0xf4};
    run_string(p,addr16,sizeof(addr16),12000,0x3000,0x8000,0);
    assert(!memcmp(p->phys_mem+0x3000,p->phys_mem+0x8000,12000));
    puts("PASS: batch and interruptible REP memory/count/flag semantics");
}
