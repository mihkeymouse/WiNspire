#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t registers[7];
#define NATIVE_TIMER_READ(offset) registers[(offset)/4]
#define NATIVE_TIMER_WRITE(offset,value) (registers[(offset)/4]=(value))
#include "../winspire-ndless/native_clock.h"
int main(void)
{
    NativeClock s={0};registers[0]=1000;registers[2]=0xe2;registers[6]=500;
    registers[1]=0xfffffffeu;
    assert(native_clock_us(&s)==0);
    native_clock_start(&s);assert(registers[0]==0xffffffffu && registers[2]==0x82);
    registers[1]-=32768;assert(native_clock_us(&s)==1000000);
    assert(native_clock_us(&s)==1000000); /* Duplicate samples do not advance. */
    registers[1]=3;s.previous=3;registers[1]=0xfffffffdu;
    uint64_t before=native_clock_us(&s);assert(s.ticks==32774);
    registers[1]-=32768;assert(native_clock_us(&s)-before==1000000);
    native_clock_start(&s);assert(s.ticks==65542); /* Start is idempotent. */
    native_clock_stop(&s);assert(registers[0]==1000 && registers[2]==0xe2 && registers[6]==500);
    native_clock_stop(&s);assert(native_clock_us(&s)==0);
    puts("PASS: Native hardware clock units, duplicate samples, wrap, lifecycle and timer register restore");
}
