#ifndef WINSPIRE_NATIVE_CLOCK_H
#define WINSPIRE_NATIVE_CLOCK_H
#include <stdint.h>
/* CX II secondary SP804 timer. Guest PIT/RTC time stays CPU-paced. */
#define NATIVE_TIMER_BASE 0x900d0020u
#define NATIVE_TIMER_HZ 32768u
#ifndef NATIVE_TIMER_READ
#define NATIVE_TIMER_READ(offset) (*(volatile uint32_t *)(NATIVE_TIMER_BASE+(offset)))
#define NATIVE_TIMER_WRITE(offset,value) (*(volatile uint32_t *)(NATIVE_TIMER_BASE+(offset))=(value))
#endif
typedef struct {
    uint32_t load, control, previous;
    uint64_t ticks;
    int active;
} NativeClock;
static inline void native_clock_start(NativeClock *s)
{
    if(s->active) return;
    s->load=NATIVE_TIMER_READ(0);s->control=NATIVE_TIMER_READ(8);
    NATIVE_TIMER_WRITE(8,0);
    NATIVE_TIMER_WRITE(0,0xffffffffu);
    NATIVE_TIMER_WRITE(8,0x82u); /* Enable, 32 bit, free-running, no IRQ. */
    s->previous=NATIVE_TIMER_READ(4);s->ticks=0;s->active=1;
}
static inline uint64_t native_clock_us(NativeClock *s)
{
    if(!s->active) return 0;
    uint32_t current=NATIVE_TIMER_READ(4);
    s->ticks+=(uint32_t)(s->previous-current); /* Includes one hardware wrap. */
    s->previous=current;
    return s->ticks*1000000ull/NATIVE_TIMER_HZ;
}
static inline void native_clock_stop(NativeClock *s)
{
    if(!s->active) return;
    NATIVE_TIMER_WRITE(8,0);
    NATIVE_TIMER_WRITE(0,s->load);
    NATIVE_TIMER_WRITE(8,s->control);
    s->active=0;
}
#endif
