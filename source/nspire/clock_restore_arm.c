#include <stdint.h>

#define SYS_EXIT 1
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_MMAP2 192

#define O_RDWR 2
#define O_SYNC 04010000
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1

#define PMU_PAGE 0x90140000UL
#define PMU_CLOCK_OFFSET 0x30U
#define CLOCK_USB_288 0x18020303U
#define CLOCK_STOCK_396 0x21020303U
#define CLOCK_OVERCLOCK_492 0x29020303U

static long syscall1(long number, long arg0)
{
	register long r0 __asm__("r0") = arg0;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory", "cc");
	return r0;
}

static long syscall3(long number, long arg0, long arg1, long arg2)
{
	register long r0 __asm__("r0") = arg0;
	register long r1 __asm__("r1") = arg1;
	register long r2 __asm__("r2") = arg2;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7)
			 : "memory", "cc");
	return r0;
}

static long syscall6(long number, long arg0, long arg1, long arg2,
		     long arg3, long arg4, long arg5)
{
	register long r0 __asm__("r0") = arg0;
	register long r1 __asm__("r1") = arg1;
	register long r2 __asm__("r2") = arg2;
	register long r3 __asm__("r3") = arg3;
	register long r4 __asm__("r4") = arg4;
	register long r5 __asm__("r5") = arg5;
	register long r7 __asm__("r7") = number;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3),
			 "r"(r4), "r"(r5), "r"(r7) : "memory", "cc");
	return r0;
}

__attribute__((noreturn))
static void exit_now(int status)
{
	(void)syscall1(SYS_EXIT, status);
	for (;;)
		;
}

static int known_clock(uint32_t value)
{
	return value == CLOCK_USB_288 || value == CLOCK_STOCK_396 ||
		value == CLOCK_OVERCLOCK_492;
}

static void settle_clock(void)
{
	register uint32_t rounds = 300000U;
//The PMU needs a short non-yielding delay post multiplier write
	__asm__ volatile(
		"1:\n"
		"subs %0,%0,#1\n"
		"bne 1b\n"
		: "+r"(rounds) : : "cc", "memory");
}

__attribute__((noreturn, noinline))
void restore_clock(void)
{
	volatile uint32_t *clock_register;
	uint32_t before;
	long mapping;
	long fd;

	fd = syscall3(SYS_OPEN, (long)"/dev/mem", O_RDWR | O_SYNC, 0);
	if (fd < 0)
		exit_now(2);
	mapping = syscall6(SYS_MMAP2, 0, 4096, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, PMU_PAGE >> 12);
	(void)syscall1(SYS_CLOSE, fd);
	if ((unsigned long)mapping >= (unsigned long)-4095L)
		exit_now(3);

	clock_register = (volatile uint32_t *)((uintptr_t)mapping +
		PMU_CLOCK_OFFSET);
	before = *clock_register;
	if (!known_clock(before))
		exit_now(4);
	//All accepted clock values have sane bus and divider setttings,
	if (before != CLOCK_STOCK_396)
		*clock_register = CLOCK_STOCK_396;
	settle_clock();
	exit_now(*clock_register == CLOCK_STOCK_396 ? 0 : 5);
}

__attribute__((naked, noreturn))
void _start(void)
{
	__asm__ volatile("b restore_clock");
}
