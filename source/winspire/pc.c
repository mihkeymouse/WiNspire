#include "pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef BUILD_NSPIRE
//newlib does not prodvide strdup
static char *pc_strdup(const char *value)
{
	size_t len = strlen(value) + 1;
	char *copy = malloc(len);
	if (copy)
		memcpy(copy, value, len);
	return copy;
}
#define strdup pc_strdup
#endif

static void raise_irq_i386(void *o, PicState2 *s)
{
	cpui386_raise_irq(o);
}

#if defined(USE_CPUABS)
static void raise_irq_kvm(void *o, PicState2 *s)
{
	cpukvm_raise_irq(o);
}

typedef struct CPUABS {
	void *cpu;
	void (*reset)(void *cpu);
	void (*reset_pm)(void *cpu, uint32_t start_addr);
	void (*set_gpr)(void *cpu, int i, uint32_t val);
	void (*step)(void *cpu, int stepcount);
	void (*register_mem)(void *cpu, int slot, uint32_t addr, uint32_t len,
			     void *ptr);
	void (*enable_fpu)(void *cpu);
	void (*_raise_irq)(void *, PicState2 *);
} CPUABS;

static CPUABS *cpu_new(int gen, char *phys_mem, long phys_mem_size, CPU_CB **cb)
{
	CPUABS *cpu = malloc(sizeof(CPUABS));
	if (gen < 0) {
		cpu->cpu = cpukvm_new(phys_mem, phys_mem_size, cb);
		cpu->reset = (void *) cpukvm_reset;
		cpu->reset_pm = (void *) cpukvm_reset_pm;
		cpu->set_gpr = (void *) cpukvm_set_gpr;
		cpu->step = (void *) cpukvm_step;
		cpu->register_mem = (void *) cpukvm_register_mem;
		cpu->enable_fpu = NULL;
		cpu->_raise_irq = raise_irq_kvm;
	} else {
		cpu->cpu = cpui386_new(gen, phys_mem, phys_mem_size, cb);
		cpu->reset = (void *) cpui386_reset;
		cpu->reset_pm = (void *) cpui386_reset_pm;
		cpu->set_gpr = (void *) cpui386_set_gpr;
		cpu->step = (void *) cpui386_step;
		cpu->register_mem = NULL;
		cpu->enable_fpu = (void *) cpui386_enable_fpu;
		cpu->_raise_irq = raise_irq_i386;
	}
	return cpu;
}

static void cpu_reset(CPUABS *cpu)
{
	cpu->reset(cpu->cpu);
}

static void cpu_reset_pm(CPUABS *cpu, uint32_t start_addr)
{
	cpu->reset_pm(cpu->cpu, start_addr);
}

static void cpu_set_gpr(CPUABS *cpu, int i, uint32_t val)
{
	cpu->set_gpr(cpu->cpu, i, val);
}

static void cpu_step(CPUABS *cpu, int stepcount)
{
	cpu->step(cpu->cpu, stepcount);
}

static void cpu_register_mem(CPUABS *cpu, int slot, uint32_t addr, uint32_t len,
			     void *ptr)
{
	if (cpu->register_mem)
		cpu->register_mem(cpu->cpu, slot, addr, len, ptr);
}

static void cpu_enable_fpu(CPUABS *cpu)
{
	if (cpu->enable_fpu)
		cpu->enable_fpu(cpu->cpu);
}

#define raise_irq(o) ((o)->_raise_irq)
#define raise_irq_param(o) ((o)->cpu)
#else
#define cpu_reset cpui386_reset
#define cpu_reset_pm cpui386_reset_pm
#define cpu_set_gpr cpui386_set_gpr
#define cpu_step cpui386_step
#define cpu_register_mem(...) do {} while (0) //noop
#define cpu_enable_fpu cpui386_enable_fpu
#define cpu_new cpui386_new
#define raise_irq(...) raise_irq_i386
#define raise_irq_param(o) (o)
#endif

#ifdef BUILD_ESP32
#ifndef MIXER_BUF_LEN
#define MIXER_BUF_LEN 128
#endif
#define PC_STEP_COUNT 512
void pcmalloc_init(void *ptr, long len);
#else
#define MIXER_BUF_LEN 2048
#define PC_STEP_COUNT 10240
#define pcmalloc_init(ptr, len)
#endif

#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
//Keep nspire batches short so guest time, input and vga retrance are serviced frequently enough to satisfy BIOS/Windows polling loops

#undef PC_STEP_COUNT
#ifdef TINY386_PC_STEP_COUNT
#define PC_STEP_COUNT TINY386_PC_STEP_COUNT
#elif defined(TINY386_HEADLESS_DIAG)
#define PC_STEP_COUNT 65536
#elif defined(TINY386_SPEED_BUILD)
#define PC_STEP_COUNT 65536
#else
#define PC_STEP_COUNT 32768
#endif
#endif

static u8 pc_io_read(void *o, int addr)
{
	PC *pc = o;
	u8 val;

	switch(addr) {
	case 0x20: case 0x21: case 0xa0: case 0xa1:
		val = i8259_ioport_read(pc->pic, addr);
		break;
	case 0x3f8: case 0x3f9: case 0x3fa: case 0x3fb:
	case 0x3fc: case 0x3fd: case 0x3fe: case 0x3ff:
		val = 0xff;
		if (pc->enable_serial)
			val = u8250_reg_read(pc->serial, addr - 0x3f8);
		break;
	case 0x2f8: case 0x2f9: case 0x2fa: case 0x2fb:
	case 0x2fc: case 0x2fd: case 0x2fe: case 0x2ff:
	case 0x2e8: case 0x2e9: case 0x2ea: case 0x2eb:
	case 0x2ec: case 0x2ed: case 0x2ee: case 0x2ef:
	case 0x3e8: case 0x3e9: case 0x3ea: case 0x3eb:
	case 0x3ec: case 0x3ed: case 0x3ee: case 0x3ef:
		val = 0;
		break;
	case 0x42:
		/* read delay for PIT channel 2 */
		/* certain guest code needs it to drive pc speaker properly */
#ifndef BUILD_NSPIRE
		usleep(0);
#endif
		/* fall through */
	case 0x40: case 0x41: case 0x43:
		val = i8254_ioport_read(pc->pit, addr);
		break;
	case 0x70: case 0x71:
		val = cmos_ioport_read(pc->cmos, addr);
		break;
	case 0x1f0: case 0x1f1: case 0x1f2: case 0x1f3:
	case 0x1f4: case 0x1f5: case 0x1f6: case 0x1f7:
		val = ide_ioport_read(pc->ide, addr - 0x1f0);
		break;
	case 0x170: case 0x171: case 0x172: case 0x173:
	case 0x174: case 0x175: case 0x176: case 0x177:
		val = ide_ioport_read(pc->ide2, addr - 0x170);
		break;
	case 0x3f6:
		val = ide_status_read(pc->ide);
		break;
	case 0x376:
		val = ide_status_read(pc->ide2);
		break;
	case 0x3c0: case 0x3c1: case 0x3c2: case 0x3c3:
	case 0x3c4: case 0x3c5: case 0x3c6: case 0x3c7:
	case 0x3c8: case 0x3c9: case 0x3ca: case 0x3cb:
	case 0x3cc: case 0x3cd: case 0x3ce: case 0x3cf:
	case 0x3d0: case 0x3d1: case 0x3d2: case 0x3d3:
	case 0x3d4: case 0x3d5: case 0x3d6: case 0x3d7:
	case 0x3d8: case 0x3d9: case 0x3da: case 0x3db:
	case 0x3dc: case 0x3dd: case 0x3de: case 0x3df:
		val = vga_ioport_read(pc->vga, addr);
		break;
	case 0x92:
		val = pc->port92;
		break;
	case 0x60:
		val = kbd_read_data(pc->i8042, addr);
		break;
	case 0x64:
		val = kbd_read_status(pc->i8042, addr);
		break;
	case 0x61:
		val = pcspk_ioport_read(pc->pcspk);
		break;
	case 0x220: case 0x221: case 0x222: case 0x223:
	case 0x228: case 0x229:
	case 0x388: case 0x389: case 0x38a: case 0x38b:
#ifdef LEAN_DEVICES
		val = 0xff;
#else
		val = adlib_read(pc->adlib, addr);
#endif
		break;
	case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
		val = i440fx_read_data(pc->i440fx, addr - 0xcfc, 0);
		break;
	case 0x300: case 0x301: case 0x302: case 0x303:
	case 0x304: case 0x305: case 0x306: case 0x307:
	case 0x308: case 0x309: case 0x30a: case 0x30b:
	case 0x30c: case 0x30d: case 0x30e: case 0x30f:
#ifdef LEAN_DEVICES
		val = 0xff;
#else
		val = ne2000_ioport_read(pc->ne2000, addr);
#endif
		break;
	case 0x310:
#ifdef LEAN_DEVICES
		val = 0xff;
#else
		val = ne2000_asic_ioport_read(pc->ne2000, addr);
#endif
		break;
	case 0x31f:
#ifdef LEAN_DEVICES
		val = 0xff;
#else
		val = ne2000_reset_ioport_read(pc->ne2000, addr);
#endif
		break;
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x05: case 0x06: case 0x07:
		val = i8257_read_chan(pc->isa_dma, addr - 0x00, 1);
		break;
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		val = i8257_read_cont(pc->isa_dma, addr - 0x08, 1);
		break;
	case 0x81: case 0x82: case 0x83: case 0x87:
		val = i8257_read_page(pc->isa_dma, addr - 0x80);
		break;
	case 0x481: case 0x482: case 0x483: case 0x487:
		val = i8257_read_pageh(pc->isa_dma, addr - 0x480);
		break;
	case 0xc0: case 0xc2: case 0xc4: case 0xc6:
	case 0xc8: case 0xca: case 0xcc: case 0xce:
		val = i8257_read_chan(pc->isa_hdma, addr - 0xc0, 1);
		break;
	case 0xd0: case 0xd2: case 0xd4: case 0xd6:
	case 0xd8: case 0xda: case 0xdc: case 0xde:
		val = i8257_read_cont(pc->isa_hdma, addr - 0xd0, 1);
		break;
	case 0x89: case 0x8a: case 0x8b: case 0x8f:
		val = i8257_read_page(pc->isa_hdma, addr - 0x88);
		break;
	case 0x489: case 0x48a: case 0x48b: case 0x48f:
		val = i8257_read_pageh(pc->isa_hdma, addr - 0x488);
		break;
	case 0x225:
#ifdef LEAN_DEVICES
		val = 0xff;
#else
		val = sb16_mixer_read(pc->sb16, addr);
#endif
		break;
	case 0x226: case 0x22a: case 0x22c: case 0x22d: case 0x22e: case 0x22f:
#ifdef LEAN_DEVICES
		val = 0xff;
#else
		val = sb16_dsp_read(pc->sb16, addr);
#endif
		break;
	case 0xf1f4:
		val = 0;
		emulink_data_read_string(pc->emulink, &val, 1, 1);
		break;
	default:
		//fprintf(stderr, "in 0x%x <= 0x%x\n", addr, 0xff);
		val = 0xff;
		break;
	}
	return val;
}

static u16 pc_io_read16(void *o, int addr)
{
	PC *pc = o;
	u16 val;

	switch(addr) {
	case 0x1ce: case 0x1cf:
		val = vbe_read(pc->vga, addr - 0x1ce);
		break;
	case 0x1f0:
		val = ide_data_readw(pc->ide);
		break;
	case 0x170:
		val = ide_data_readw(pc->ide2);
		break;
	case 0xcf8:
		val = i440fx_read_addr(pc->i440fx, 0, 1);
		break;
	case 0xcfc: case 0xcfe:
		val = i440fx_read_data(pc->i440fx, addr - 0xcfc, 1);
		break;
	case 0x310:
#ifdef LEAN_DEVICES
		val = 0xffff;
#else
		val = ne2000_asic_ioport_read(pc->ne2000, addr);
#endif
		break;
	case 0x220:
#ifdef LEAN_DEVICES
		val = 0xffff;
#else
		val = adlib_read(pc->adlib, addr);
#endif
		break;
	default:
#if !defined(BUILD_NSPIRE) || !defined(TINY386_SPEED_BUILD)
		fprintf(stderr, "inw 0x%x <= 0x%x\n", addr, 0xffff);
#endif
		val = 0xffff;
		break;
	}
	return val;
}

static u32 pc_io_read32(void *o, int addr)
{
	PC *pc = o;
	u32 val;
	switch(addr) {
	case 0x1f0:
		val = ide_data_readl(pc->ide);
		break;
	case 0x170:
		val = ide_data_readl(pc->ide2);
		break;
	case 0x3cc:
		val = (get_uticks() - pc->boot_start_time) / 1000;
		break;
	case 0xcf8:
		val = i440fx_read_addr(pc->i440fx, 0, 2);
		break;
	case 0xcfc:
		val = i440fx_read_data(pc->i440fx, 0, 2);
		break;
	case 0xf1f0:
		val = emulink_status_read(pc->emulink);
		break;
	default:
#if !defined(BUILD_NSPIRE) || !defined(TINY386_SPEED_BUILD)
		fprintf(stderr, "ind 0x%x <= 0x%x\n", addr, 0xffffffff);
#endif
		val = 0xffffffff;
		break;
	}
	return val;
}

static int pc_io_read_string(void *o, int addr, uint8_t *buf, int size, int count)
{
	PC *pc = o;
	switch(addr) {
	case 0x1f0:
		return ide_data_read_string(pc->ide, buf, size, count);
	case 0x170:
		return ide_data_read_string(pc->ide2, buf, size, count);
	case 0xf1f4:
		return emulink_data_read_string(pc->emulink, buf, size, count);
	}
	return 0;
}

static void pc_io_write(void *o, int addr, u8 val)
{
	PC *pc = o;
	switch(addr) {
	case 0x80: case 0xed:
		/* used by linux, for io delay */
		return;
	case 0x20: case 0x21: case 0xa0: case 0xa1:
		i8259_ioport_write(pc->pic, addr, val);
		return;
	case 0x3f8: case 0x3f9: case 0x3fa: case 0x3fb:
	case 0x3fc: case 0x3fd: case 0x3fe: case 0x3ff:
		u8250_reg_write(pc->serial, addr - 0x3f8, val);
		return;
	case 0x2f8: case 0x2f9: case 0x2fa: case 0x2fb:
	case 0x2fc: case 0x2fd: case 0x2fe: case 0x2ff:
	case 0x2e8: case 0x2e9: case 0x2ea: case 0x2eb:
	case 0x2ec: case 0x2ed: case 0x2ee: case 0x2ef:
	case 0x3e8: case 0x3e9: case 0x3ea: case 0x3eb:
	case 0x3ec: case 0x3ed: case 0x3ee: case 0x3ef:
		return;
	case 0x40: case 0x41: case 0x42: case 0x43:
		i8254_ioport_write(pc->pit, addr, val);
		return;
	case 0x70: case 0x71:
		cmos_ioport_write(pc->cmos, addr, val);
		return;
	case 0x1f0: case 0x1f1: case 0x1f2: case 0x1f3:
	case 0x1f4: case 0x1f5: case 0x1f6: case 0x1f7:
		ide_ioport_write(pc->ide, addr - 0x1f0, val);
		return;
	case 0x170: case 0x171: case 0x172: case 0x173:
	case 0x174: case 0x175: case 0x176: case 0x177:
		ide_ioport_write(pc->ide2, addr - 0x170, val);
		return;
	case 0x3f6:
		ide_cmd_write(pc->ide, val);
		return;
	case 0x376:
		ide_cmd_write(pc->ide2, val);
		return;
	case 0x3c0: case 0x3c1: case 0x3c2: case 0x3c3:
	case 0x3c4: case 0x3c5: case 0x3c6: case 0x3c7:
	case 0x3c8: case 0x3c9: case 0x3ca: case 0x3cb:
	case 0x3cc: case 0x3cd: case 0x3ce: case 0x3cf:
	case 0x3d0: case 0x3d1: case 0x3d2: case 0x3d3:
	case 0x3d4: case 0x3d5: case 0x3d6: case 0x3d7:
	case 0x3d8: case 0x3d9: case 0x3da: case 0x3db:
	case 0x3dc: case 0x3dd: case 0x3de: case 0x3df:
		vga_ioport_write(pc->vga, addr, val);
		return;
	case 0x402:
#if !defined(BUILD_NSPIRE) || !defined(TINY386_SPEED_BUILD)
		putchar(val);
		fflush(stdout);
#else
		(void)val;
#endif
		return;
	case 0x92:
		pc->port92 = val;
		return;
	case 0x60:
		kbd_write_data(pc->i8042, addr, val);
		return;
	case 0x64:
		kbd_write_command(pc->i8042, addr, val);
		return;
	case 0x61:
		pcspk_ioport_write(pc->pcspk, val);
		return;
	case 0x220: case 0x221: case 0x222: case 0x223:
	case 0x228: case 0x229:
	case 0x388: case 0x389: case 0x38a: case 0x38b:
#ifndef LEAN_DEVICES
		adlib_write(pc->adlib, addr, val);
#endif
		return;
	case 0x8900:
		switch (val) {
		case 'S': if (pc->shutdown_state == 0) pc->shutdown_state = 1; break;
		case 'h': if (pc->shutdown_state == 1) pc->shutdown_state = 2; break;
		case 'u': if (pc->shutdown_state == 2) pc->shutdown_state = 3; break;
		case 't': if (pc->shutdown_state == 3) pc->shutdown_state = 4; break;
		case 'd': if (pc->shutdown_state == 4) pc->shutdown_state = 5; break;
		case 'o': if (pc->shutdown_state == 5) pc->shutdown_state = 6; break;
		case 'w': if (pc->shutdown_state == 6) pc->shutdown_state = 7; break;
		case 'n': if (pc->shutdown_state == 7) pc->shutdown_state = 8; break;
		default : pc->shutdown_state = 0; break;
		}
		return;
	case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
		i440fx_write_data(pc->i440fx, addr - 0xcfc, val, 0);
		return;
	case 0x300: case 0x301: case 0x302: case 0x303:
	case 0x304: case 0x305: case 0x306: case 0x307:
	case 0x308: case 0x309: case 0x30a: case 0x30b:
	case 0x30c: case 0x30d: case 0x30e: case 0x30f:
#ifndef LEAN_DEVICES
		ne2000_ioport_write(pc->ne2000, addr, val);
#endif
		return;
	case 0x310:
#ifndef LEAN_DEVICES
		ne2000_asic_ioport_write(pc->ne2000, addr, val);
#endif
		return;
	case 0x31f:
#ifndef LEAN_DEVICES
		ne2000_reset_ioport_write(pc->ne2000, addr, val);
#endif
		return;
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x05: case 0x06: case 0x07:
		i8257_write_chan(pc->isa_dma, addr - 0x00, val, 1);
		return;
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		i8257_write_cont(pc->isa_dma, addr - 0x08, val, 1);
		return;
	case 0x81: case 0x82: case 0x83: case 0x87:
		i8257_write_page(pc->isa_dma, addr - 0x80, val);
		return;
	case 0x481: case 0x482: case 0x483: case 0x487:
		i8257_write_pageh(pc->isa_dma, addr - 0x480, val);
		return;
	case 0xc0: case 0xc2: case 0xc4: case 0xc6:
	case 0xc8: case 0xca: case 0xcc: case 0xce:
		i8257_write_chan(pc->isa_hdma, addr - 0xc0, val, 1);
		return;
	case 0xd0: case 0xd2: case 0xd4: case 0xd6:
	case 0xd8: case 0xda: case 0xdc: case 0xde:
		i8257_write_cont(pc->isa_hdma, addr - 0xd0, val, 1);
		return;
	case 0x89: case 0x8a: case 0x8b: case 0x8f:
		i8257_write_page(pc->isa_hdma, addr - 0x88, val);
		return;
	case 0x489: case 0x48a: case 0x48b: case 0x48f:
		i8257_write_pageh(pc->isa_hdma, addr - 0x488, val);
		return;
	case 0x224:
#ifndef LEAN_DEVICES
		sb16_mixer_write_indexb(pc->sb16, addr, val);
#endif
		return;
	case 0x225:
#ifndef LEAN_DEVICES
		sb16_mixer_write_datab(pc->sb16, addr, val);
#endif
		return;
	case 0x226: case 0x22c:
#ifndef LEAN_DEVICES
		sb16_dsp_write(pc->sb16, addr, val);
#endif
		return;
	case 0xf1f4:
		emulink_data_write_string(pc->emulink, &val, 1, 1);
		return;
	default:
#if !defined(BUILD_NSPIRE) || !defined(TINY386_SPEED_BUILD)
		fprintf(stderr, "out 0x%x => 0x%x\n", val, addr);
#endif
		return;
	}
}

static void pc_io_write16(void *o, int addr, u16 val)
{
	PC *pc = o;
	switch(addr) {
	case 0x1f0:
		ide_data_writew(pc->ide, val);
		return;
	case 0x170:
		ide_data_writew(pc->ide2, val);
		return;
	case 0x3c0: case 0x3c1: case 0x3c2: case 0x3c3:
	case 0x3c4: case 0x3c5: case 0x3c6: case 0x3c7:
	case 0x3c8: case 0x3c9: case 0x3ca: case 0x3cb:
	case 0x3cc: case 0x3cd: case 0x3ce: case 0x3cf:
	case 0x3d0: case 0x3d1: case 0x3d2: case 0x3d3:
	case 0x3d4: case 0x3d5: case 0x3d6: case 0x3d7:
	case 0x3d8: case 0x3d9: case 0x3da: case 0x3db:
	case 0x3dc: case 0x3dd: case 0x3de:
		vga_ioport_write(pc->vga, addr, val & 0xff);
		vga_ioport_write(pc->vga, addr + 1, (val >> 8) & 0xff);
		return;
	case 0x1ce: case 0x1cf:
		vbe_write(pc->vga, addr - 0x1ce, val);
		return;
	case 0xcfc: case 0xcfe:
		i440fx_write_data(pc->i440fx, addr - 0xcfc, val, 1);
		return;
	case 0x310:
#ifndef LEAN_DEVICES
		ne2000_asic_ioport_write(pc->ne2000, addr, val);
#endif
		return;
	default:
#if !defined(BUILD_NSPIRE) || !defined(TINY386_SPEED_BUILD)
		fprintf(stderr, "outw 0x%x => 0x%x\n", val, addr);
#endif
		return;
	}
}

static void pc_io_write32(void *o, int addr, u32 val)
{
	PC *pc = o;
	switch(addr) {
	case 0x1f0:
		ide_data_writel(pc->ide, val);
		return;
	case 0x170:
		ide_data_writel(pc->ide2, val);
		return;
	case 0xcf8:
		i440fx_write_addr(pc->i440fx, 0, val, 2);
		return;
	case 0xcfc:
		i440fx_write_data(pc->i440fx, 0, val, 2);
		return;
	case 0xf1f0:
		emulink_cmd_write(pc->emulink, val);
		return;
	case 0xf1f4:
		emulink_data_write(pc->emulink, val);
		return;
	default:
#if !defined(BUILD_NSPIRE) || !defined(TINY386_SPEED_BUILD)
		fprintf(stderr, "outd 0x%x => 0x%x\n", val, addr);
#endif
		return;
	}
}

static int pc_io_write_string(void *o, int addr, uint8_t *buf, int size, int count)
{
	PC *pc = o;
	switch(addr) {
	case 0x1f0:
		return ide_data_write_string(pc->ide, buf, size, count);
	case 0x170:
		return ide_data_write_string(pc->ide2, buf, size, count);
	case 0xf1f4:
		return emulink_data_write_string(pc->emulink, buf, size, count);
	}
	return 0;
}

void pc_vga_step(void *o)
{
	PC *pc = o;
	int refresh = vga_step(pc->vga);
	if (refresh) {
		vga_refresh(pc->vga, pc->redraw, pc->redraw_data,
			    pc->full_update != 0);
		if (pc->full_update == 2)
			pc->full_update = 0;
	}
}

#ifndef BUILD_NSPIRE
static int desktop_step_count;

void pc_set_step_count(int count)
{
	desktop_step_count = count;
}
#endif

void pc_step(PC *pc)
{
	if (pc->reset_request) {
		pc->reset_request = 0;
		load_bios_and_reset(pc);
	}

	i8254_update_irq(pc->pit);
	cmos_update_irq(pc->cmos);
	if (pc->enable_serial)
		u8250_update(pc->serial);
	kbd_step(pc->i8042);
#ifndef BUILD_NSPIRE
	ne2000_step(pc->ne2000);
#endif
#if !defined(TINY386_SPEED_BUILD) && (!defined(BUILD_NSPIRE) || defined(NSPIRE_ENABLE_DMA_STEP))
	i8257_dma_run(pc->isa_dma);
	i8257_dma_run(pc->isa_hdma);
#endif
#ifndef BUILD_NSPIRE
	cpu_step(pc->cpu, desktop_step_count > 0 ? desktop_step_count : PC_STEP_COUNT);
#else
	cpu_step(pc->cpu, PC_STEP_COUNT);
#endif
}

#ifdef BUILD_NSPIRE
void pc_free_buffers(PC *pc)
{
	if (!pc)
		return;
	//Ndless returns back to TI-OS make sure to free all buffers.
	ide_delete(pc->ide);
	ide_delete(pc->ide2);
	i8042_delete(pc->i8042);
#ifndef LEAN_DEVICES
	adlib_free(pc->adlib);
	ne2000_delete(pc->ne2000);
	sb16_delete(pc->sb16);
#endif
	i8257_delete(pc->isa_dma);
	i8257_delete(pc->isa_hdma);
	pcspk_delete(pc->pcspk);
	emulink_delete(pc->emulink);
	i440fx_delete(pc->i440fx);
	u8250_delete(pc->serial);
	cmos_delete(pc->cmos);
	i8254_delete(pc->pit);
	i8259_delete(pc->pic);
	cpui386_delete(pc->cpu);
	vga_delete(pc->vga);
	free(pc->vga_mem);
	free(pc->phys_mem);
	free(pc);
}
#endif

static int read_irq(void *o)
{
	PicState2 *s = o;
	return i8259_read_irq(s);
}

static void set_irq(void *o, int irq, int level)
{
	PicState2 *s = o;
	return i8259_set_irq(s, irq, level);
}

static void set_pci_vga_bar(void *opaque, int bar_num, uint32_t addr, bool enabled)
{
	PC *pc = opaque;
	if (enabled)
		pc->pci_vga_ram_addr = addr;
	else
		pc->pci_vga_ram_addr = -1;

	if (enabled)
		cpu_register_mem(pc->cpu, 2, addr, pc->vga_mem_size,
				 pc->vga_mem);
	else
		cpu_register_mem(pc->cpu, 2, addr, 0,
				 NULL);
}

#define VBE_LFB_ADDR 0xe0000000U

static bool pci_vga_addr(PC *pc, uword addr, uword *offset)
{
	uword vga_addr = pc->pci_vga_ram_addr;

	if (vga_addr != (uword)-1 && addr >= vga_addr) {
		uword candidate = addr - vga_addr;
		if (candidate < pc->vga_mem_size) {
			*offset = candidate;
			return true;
		}
	}
	if (addr >= VBE_LFB_ADDR) {
		uword candidate = addr - VBE_LFB_ADDR;
		if (candidate < pc->vga_mem_size) {
#if !defined(BUILD_NSPIRE)
			static bool logged_fixed_lfb;
			if (!logged_fixed_lfb) {
				fprintf(stderr,
					"[vbe-lfb] first fixed-alias access phys=0x%08x offset=0x%08x\n",
					addr, candidate);
				fflush(stderr);
				logged_fixed_lfb = true;
			}
#endif
			*offset = candidate;
			return true;
		}
	}
	return false;
}

static u8 iomem_read8(void *iomem, uword addr)
{
	PC *pc = iomem;
	uword offset;
	if (pci_vga_addr(pc, addr, &offset))
		return pc->vga_mem[offset];
	return vga_mem_read(pc->vga, addr - 0xa0000);
}

static void iomem_write8(void *iomem, uword addr, u8 val)
{
	PC *pc = iomem;
	uword offset;
	if (pci_vga_addr(pc, addr, &offset)) {
		pc->vga_mem[offset] = val;
		return;
	}
	vga_mem_write(pc->vga, addr - 0xa0000, val);
}

static u16 iomem_read16(void *iomem, uword addr)
{
	return iomem_read8(iomem, addr) |
		((u16) iomem_read8(iomem, addr + 1) << 8);
}

static void iomem_write16(void *iomem, uword addr, u16 val)
{
	PC *pc = iomem;
	uword offset;
	if (pci_vga_addr(pc, addr, &offset)) {
		if (offset + 1 < pc->vga_mem_size) {
#ifdef BUILD_NSPIRE
			pc->vga_mem[offset] = val;
			pc->vga_mem[offset + 1] = val >> 8;
#else
			*(uint16_t *)&(pc->vga_mem[offset]) = val;
#endif
		}
		return;
	}
	vga_mem_write16(pc->vga, addr - 0xa0000, val);
}

static u32 iomem_read32(void *iomem, uword addr)
{
	return iomem_read16(iomem, addr) |
		((u32) iomem_read16(iomem, addr + 2) << 16);
}

static void iomem_write32(void *iomem, uword addr, u32 val)
{
	PC *pc = iomem;
	uword offset;
	if (pci_vga_addr(pc, addr, &offset)) {
		if (offset + 3 < pc->vga_mem_size) {
#ifdef BUILD_NSPIRE
			pc->vga_mem[offset] = val;
			pc->vga_mem[offset + 1] = val >> 8;
			pc->vga_mem[offset + 2] = val >> 16;
			pc->vga_mem[offset + 3] = val >> 24;
#else
			*(uint32_t *)&(pc->vga_mem[offset]) = val;
#endif
		}
		return;
	}
	vga_mem_write32(pc->vga, addr - 0xa0000, val);
}

static bool iomem_write_string(void *iomem, uword addr, uint8_t *buf, int len)
{
	PC *pc = iomem;
	uword offset;
	if (pci_vga_addr(pc, addr, &offset)) {
		if (len >= 0 && offset <= pc->vga_mem_size &&
		    (uword)len <= pc->vga_mem_size - offset) {
			memcpy(pc->vga_mem + offset, buf, len);
			return true;
		}
		return false;
	}
	return vga_mem_write_string(pc->vga, addr - 0xa0000, buf, len);
}

static void pc_reset_request(void *p)
{
	PC *pc = p;
	pc->reset_request = 1;
}

PC *pc_new(SimpleFBDrawFunc *redraw, void *redraw_data,
	   u8 *fb, PCConfig *conf)
{
	PC *pc = malloc(sizeof(PC));
	char *mem = bigmalloc(conf->mem_size);
	CPU_CB *cb = NULL;
	memset(mem, 0, conf->mem_size);
	pcmalloc_init(mem + 0xa0000, 0xc0000 - 0xa0000);
	pc->cpu = cpu_new(conf->cpu_gen, mem, conf->mem_size, &cb);
	if (conf->fpu)
		cpu_enable_fpu(pc->cpu);
	pc->bios = conf->bios;
	pc->vga_bios = conf->vga_bios;
	pc->linuxstart = conf->linuxstart;
	if (!pc->bios) pc->bios = "bios.bin";
	if (!pc->vga_bios) pc->vga_bios = "vgabios.bin";
	if (!pc->linuxstart) pc->linuxstart = "linuxstart.bin";
	pc->kernel = conf->kernel;
	pc->initrd = conf->initrd;
	pc->cmdline = conf->cmdline;
	pc->enable_serial = conf->enable_serial;
#if !defined(_WIN32) && !defined(__wasm__) && !defined(BUILD_NSPIRE)
	if (pc->enable_serial)
		CaptureKeyboardInput();
#endif
	pc->full_update = 0;

	pc->pic = i8259_init(raise_irq(pc->cpu), raise_irq_param(pc->cpu));
	cb->pic = pc->pic;
	cb->pic_read_irq = read_irq;

	pc->pit = i8254_init(0, pc->pic, set_irq);
	pc->serial = u8250_init(4, pc->pic, set_irq);
	pc->cmos = cmos_init(conf->mem_size, 8, pc->pic, set_irq);
	pc->ide = ide_allocate(14, pc->pic, set_irq);
	pc->ide2 = ide_allocate(15, pc->pic, set_irq);
	const char **disks = conf->disks;
	for (int i = 0; i < 4; i++) {
		if (!disks[i] || disks[i][0] == 0)
			continue;
		int ret;
		if (i < 2) {
			if (conf->iscd[i])
				ret = ide_attach_cd(pc->ide, i, disks[i]);
			else
				ret = ide_attach(pc->ide, i, disks[i]);
		} else {
			if (conf->iscd[i])
				ret = ide_attach_cd(pc->ide2, i - 2, disks[i]);
			else
				ret = ide_attach(pc->ide2, i - 2, disks[i]);
		}
		if (ret != 0) {
			fprintf(stderr, "FATAL: %s attach failed: drive=%d path=%s\n",
				conf->iscd[i] ? "cd" : "disk", i, disks[i]);
			return NULL;
		}
	}

	if (conf->fill_cmos)
		ide_fill_cmos(pc->ide, pc->cmos, cmos_set);
	//SeaBIOS reads QEMU-compatible translation byte independently
	cmos_set(pc->cmos, 0x39, ide_cmos_disk_translation(pc->ide));

	int piix3_devfn;
	pc->i440fx = i440fx_init(&pc->pcibus, &piix3_devfn);
	pc->pci_ide = piix3_ide_init(pc->pcibus, piix3_devfn + 1);

	pc->phys_mem = mem;
	pc->phys_mem_size = conf->mem_size;

	cb->io = pc;
	cb->io_read8 = pc_io_read;
	cb->io_write8 = pc_io_write;
	cb->io_read16 = pc_io_read16;
	cb->io_write16 = pc_io_write16;
	cb->io_read32 = pc_io_read32;
	cb->io_write32 = pc_io_write32;
	cb->io_read_string = pc_io_read_string;
	cb->io_write_string = pc_io_write_string;

	pc->boot_start_time = 0;

	pc->vga_mem_size = conf->vga_mem_size;
	pc->vga_mem = bigmalloc(pc->vga_mem_size);
	memset(pc->vga_mem, 0, pc->vga_mem_size);
	pc->vga = vga_init(pc->vga_mem, pc->vga_mem_size,
			   fb, conf->width, conf->height);
	vga_set_force_8dm(pc->vga, conf->vga_force_8dm);
	pc->pci_vga = vga_pci_init(pc->vga, pc->pcibus, pc, set_pci_vga_bar);
	pc->pci_vga_ram_addr = -1;

	pc->emulink = emulink_init();
	const char **fdd = conf->fdd;
	for (int i = 0; i < 2; i++) {
		if (!fdd[i] || fdd[i][0] == 0)
			continue;
		int ret;
		ret = emulink_attach_floppy(pc->emulink, i, fdd[i]);
		assert(ret == 0);
	}

	cb->iomem = pc;
	cb->iomem_read8 = iomem_read8;
	cb->iomem_write8 = iomem_write8;
	cb->iomem_read16 = iomem_read16;
	cb->iomem_write16 = iomem_write16;
	cb->iomem_read32 = iomem_read32;
	cb->iomem_write32 = iomem_write32;
	cb->iomem_write_string = iomem_write_string;

	pc->redraw = redraw;
	pc->redraw_data = redraw_data;

	pc->i8042 = i8042_init(&(pc->kbd), &(pc->mouse),
			       1, 12, pc->pic, set_irq,
			       pc, pc_reset_request);
#ifndef LEAN_DEVICES
	pc->adlib = adlib_new();
	pc->ne2000 = isa_ne2000_init(0x300, 9, pc->pic, set_irq);
#else
	pc->adlib = NULL;
	pc->ne2000 = NULL;
#endif
	pc->isa_dma = i8257_new(pc->phys_mem, pc->phys_mem_size,
				0x00, 0x80, 0x480, 0);
	pc->isa_hdma = i8257_new(pc->phys_mem, pc->phys_mem_size,
				 0xc0, 0x88, 0x488, 1);
#ifndef LEAN_DEVICES
	pc->sb16 = sb16_new(0x220, 5,
			    pc->isa_dma, pc->isa_hdma,
			    pc->pic, set_irq);
#else
	pc->sb16 = NULL;
#endif
	pc->pcspk = pcspk_init(pc->pit);
	pc->port92 = 0x2;
	pc->shutdown_state = 0;
	pc->reset_request = 0;
	return pc;
}

void mixer_callback (void *opaque, uint8_t *stream, int free)
{
#ifdef LEAN_DEVICES
	(void)opaque;
	memset(stream, 0, free);
#else
	uint8_t tmpbuf[MIXER_BUF_LEN];
	PC *pc = opaque;
	assert(free / 2 <= MIXER_BUF_LEN);
	memset(tmpbuf, 0, MIXER_BUF_LEN);
	adlib_callback(pc->adlib, tmpbuf, free / 2); // s16, mono
	sb16_audio_callback(pc->sb16, stream, free); // s16, stereo

	int16_t *d2 = (int16_t *) stream;
	int16_t *d1 = (int16_t *) tmpbuf;
	for (int i = 0; i < free / 2; i++) {
		int res = d2[i] + d1[i / 2];
		if (res > 32767) res = 32767;
		if (res < -32768) res = -32768;
		d2[i] = res;
	}

	if (pcspk_get_active_out(pc->pcspk)) {
		memset(tmpbuf, 0x80, MIXER_BUF_LEN / 2);
		pcspk_callback(pc->pcspk, tmpbuf, free / 4); // u8, mono
		for (int i = 0; i < free / 2; i++) {
			int res = d2[i];
			res += ((int) tmpbuf[i / 2] - 0x80) << 5;
			if (res > 32767) res = 32767;
			if (res < -32768) res = -32768;
			d2[i] = res;
		}
	}
#endif
}

void load_bios_and_reset(PC *pc)
{
	if (pc->bios && pc->bios[0])
		load_rom(pc->phys_mem, pc->bios, 0x100000, 1);
	if (pc->vga_bios && pc->vga_bios[0])
		load_rom(pc->phys_mem, pc->vga_bios, 0xc0000, 0);
	if (pc->kernel && pc->kernel[0]) {
		int start_addr = 0x10000;
		int cmdline_addr = 0xf800;
		int kernel_size = load_rom(pc->phys_mem, pc->kernel, 0x00100000, 0);
		int initrd_size = 0;
		if (pc->initrd && pc->initrd[0])
			initrd_size = load_rom(pc->phys_mem, pc->initrd, 0x00400000, 0);
		if (pc->cmdline && pc->cmdline[0])
			strcpy(pc->phys_mem + cmdline_addr, pc->cmdline);
		else
			strcpy(pc->phys_mem + cmdline_addr, "");

		load_rom(pc->phys_mem, pc->linuxstart, start_addr, 0);
		cpu_reset_pm(pc->cpu, 0x10000);
		cpu_set_gpr(pc->cpu, 0, pc->phys_mem_size);
		cpu_set_gpr(pc->cpu, 3, initrd_size);
		cpu_set_gpr(pc->cpu, 1, cmdline_addr);
		cpu_set_gpr(pc->cpu, 2, kernel_size);
	} else {
		cpu_reset(pc->cpu);
	}
}

static long parse_mem_size(const char *value)
{
	int len = strlen(value);
	long a = atol(value);
	if (len) {
		switch (value[len - 1]) {
		case 'G': a *= 1024 * 1024 * 1024; break;
		case 'M': a *= 1024 * 1024; break;
		case 'K': a *= 1024; break;
		}
	}
	return a;
}

int parse_conf_ini(void* user, const char* section,
		   const char* name, const char* value)
{
	PCConfig *conf = user;
#define SEC(a) (strcmp(section, a) == 0)
#define NAME(a) (strcmp(name, a) == 0)
	if (SEC("pc")) {
		if (NAME("bios")) {
			conf->bios = strdup(value);
		} else if (NAME("vga_bios")) {
			conf->vga_bios = strdup(value);
		} else if (NAME("mem_size")) {
			conf->mem_size = parse_mem_size(value);
		} else if (NAME("vga_mem_size")) {
			conf->vga_mem_size = parse_mem_size(value);
		} else if (NAME("hda")) {
			conf->disks[0] = strdup(value);
			conf->iscd[0] = 0;
		} else if (NAME("hdb")) {
			conf->disks[1] = strdup(value);
			conf->iscd[1] = 0;
		} else if (NAME("hdc")) {
			conf->disks[2] = strdup(value);
			conf->iscd[2] = 0;
		} else if (NAME("hdd")) {
			conf->disks[3] = strdup(value);
			conf->iscd[3] = 0;
		} else if (NAME("cda")) {
			conf->disks[0] = strdup(value);
			conf->iscd[0] = 1;
		} else if (NAME("cdb")) {
			conf->disks[1] = strdup(value);
			conf->iscd[1] = 1;
		} else if (NAME("cdc")) {
			conf->disks[2] = strdup(value);
			conf->iscd[2] = 1;
		} else if (NAME("cdd")) {
			conf->disks[3] = strdup(value);
			conf->iscd[3] = 1;
		} else if (NAME("fda")) {
			conf->fdd[0] = strdup(value);
		} else if (NAME("fdb")) {
			conf->fdd[1] = strdup(value);
		} else if (NAME("fill_cmos")) {
			conf->fill_cmos = atoi(value);
		} else if (NAME("linuxstart")) {
			conf->linuxstart = strdup(value);
		} else if (NAME("kernel")) {
			conf->kernel = strdup(value);
		} else if (NAME("initrd")) {
			conf->initrd = strdup(value);
		} else if (NAME("cmdline")) {
			conf->cmdline = strdup(value);
		} else if (NAME("enable_serial")) {
			conf->enable_serial = atoi(value);
		} else if (NAME("vga_force_8dm")) {
			conf->vga_force_8dm = atoi(value);
		}
	} else if (SEC("display")) {
		if (NAME("width")) {
			conf->width = atoi(value);
		} else if (NAME("height")) {
			conf->height = atoi(value);
		}
	} else if (SEC("cpu")) {
		if (NAME("gen")) {
			conf->cpu_gen = atoi(value);
		} else if (NAME("fpu")) {
			conf->fpu = atoi(value);
		} else if (NAME("clock_hz")) {
			conf->clock_hz = (uint32_t)strtoul(value, NULL, 0);
		}
	}
#undef SEC
#undef NAME
	return 1;
}
