#include <libndls.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pc.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FRAMEBUFFER_BYTES \
	(SCREEN_WIDTH * SCREEN_HEIGHT * 2)
#ifndef TINY386_INPUT_POLL_LOOPS
/* Keep the proven sampling interval for the touchpad deadzone. */
#define TINY386_INPUT_POLL_LOOPS 2U
#endif
#ifndef TINY386_VIDEO_POLL_LOOPS
#define TINY386_VIDEO_POLL_LOOPS 1U
#endif
#define INPUT_POLL_LOOPS TINY386_INPUT_POLL_LOOPS
#define VIDEO_POLL_LOOPS TINY386_VIDEO_POLL_LOOPS
#if INPUT_POLL_LOOPS == 0 || \
	(INPUT_POLL_LOOPS & (INPUT_POLL_LOOPS - 1)) != 0
#error TINY386_INPUT_POLL_LOOPS must be a nonzero power of two
#endif
#if VIDEO_POLL_LOOPS == 0 || \
	(VIDEO_POLL_LOOPS & (VIDEO_POLL_LOOPS - 1)) != 0
#error TINY386_VIDEO_POLL_LOOPS must be a nonzero power of two
#endif
#define LCD_RETRY_MS 250ULL
#define LCD_POLL_MS 100ULL
#define POLL_MAX 1024U
#define CPU_HZ 4770000U
#define CPU_HZ_MIN 1000000U
#define CPU_HZ_MAX 100000000U
#define INS_CYCLES 12ULL
#define IDLE_INS 12288ULL
#define CURSOR_REG 0xC0000C00U
#define REDRAW_AREA \
	((SCREEN_WIDTH * SCREEN_HEIGHT * 3) / 4)
#define PAD_DEADZONE 2
#define PAD_SCALE 18
#define PAD_ACCEL_AT 80
#define PAD_ACCEL_SCALE 26
#define MOUSE_DELTA_MAX 96
#define BTN_LEFT 0x01
#define BTN_RIGHT 0x02
#define KEY_SEMICOLON 0x27
#define GUEST_RAM_MIN (4L * 1024 * 1024)
#define GUEST_RAM_MAX (32L * 1024 * 1024)
#define VGA_RAM_MIN (64L * 1024)
#define VGA_RAM_MAX (1024L * 1024)

typedef struct {
	u8 *framebuffer;
	bool ready;
	bool lcd_active;
	bool dirty;
	bool full_redraw;
	int dirty_x;
	int dirty_y;
	int dirty_width;
	int dirty_height;
	uint64_t last_claim_ms;
	uint64_t last_draw_ms;
} Display;

typedef struct {
	const t_key *key;
	int keycode;
	bool is_pressed;
} KeyBinding;

typedef struct {
	bool has_position;
	uint16_t last_x;
	uint16_t last_y;
	int x_remainder;
	int y_remainder;
	int buttons;
	int arrows;
} TouchState;

#define TOUCH_ARROW_UP    (1 << 0)
#define TOUCH_ARROW_DOWN  (1 << 1)
#define TOUCH_ARROW_LEFT  (1 << 2)
#define TOUCH_ARROW_RIGHT (1 << 3)

static void *reserved_ram;
static size_t reserved_ram_size;
static void *reserved_vram;
static size_t reserved_vram_size;
static uint32_t guest_ticks;
static uint32_t last_cycle;
static uint64_t tick_remainder;
static uint32_t guest_hz = CPU_HZ;
static uint32_t input_poll_loops = INPUT_POLL_LOOPS;
static uint32_t video_poll_loops = VIDEO_POLL_LOOPS;
static volatile bool mode_changed;
static TouchState touchpad_state;

static bool is_power_of_two(uint32_t value)
{
	return value && !(value & (value - 1));
}

static int parse_native_config(void *user, const char *section,
		const char *name, const char *value)
{
	if (!strcmp(section, "nspire")) {
		if (!strcmp(name, "input_poll_loops"))
			input_poll_loops = (uint32_t)strtoul(value, NULL, 0);
		else if (!strcmp(name, "video_poll_loops"))
			video_poll_loops = (uint32_t)strtoul(value, NULL, 0);
		return 1;
	}
	return parse_conf_ini(user, section, name, value);
}

/*
 * Map physical Nspire keys to Linux input keycodes. The shared PS/2 layer
 * converts navigation keys to E0-prefixed PC set-1 scancodes. Touchpad arrow
 * zones use the same keycodes.
 */
static KeyBinding keys[] = {
	{ &KEY_NSPIRE_ESC, 1, false },
	{ &KEY_NSPIRE_1, 2, false },
	{ &KEY_NSPIRE_2, 3, false },
	{ &KEY_NSPIRE_3, 4, false },
	{ &KEY_NSPIRE_4, 5, false },
	{ &KEY_NSPIRE_5, 6, false },
	{ &KEY_NSPIRE_6, 7, false },
	{ &KEY_NSPIRE_7, 8, false },
	{ &KEY_NSPIRE_8, 9, false },
	{ &KEY_NSPIRE_9, 10, false },
	{ &KEY_NSPIRE_0, 11, false },
	{ &KEY_NSPIRE_MINUS, 12, false },
	{ &KEY_NSPIRE_EQU, 13, false },
	{ &KEY_NSPIRE_DEL, 14, false },
	{ &KEY_NSPIRE_TAB, 15, false },
	{ &KEY_NSPIRE_Q, 16, false },
	{ &KEY_NSPIRE_W, 17, false },
	{ &KEY_NSPIRE_E, 18, false },
	{ &KEY_NSPIRE_R, 19, false },
	{ &KEY_NSPIRE_T, 20, false },
	{ &KEY_NSPIRE_Y, 21, false },
	{ &KEY_NSPIRE_U, 22, false },
	{ &KEY_NSPIRE_I, 23, false },
	{ &KEY_NSPIRE_O, 24, false },
	{ &KEY_NSPIRE_P, 25, false },
	{ &KEY_NSPIRE_ENTER, 28, false },
	{ &KEY_NSPIRE_CTRL, 29, false },
	{ &KEY_NSPIRE_A, 30, false },
	{ &KEY_NSPIRE_S, 31, false },
	{ &KEY_NSPIRE_D, 32, false },
	{ &KEY_NSPIRE_F, 33, false },
	{ &KEY_NSPIRE_G, 34, false },
	{ &KEY_NSPIRE_H, 35, false },
	{ &KEY_NSPIRE_J, 36, false },
	{ &KEY_NSPIRE_K, 37, false },
	{ &KEY_NSPIRE_L, 38, false },
	{ &KEY_NSPIRE_SHIFT, 42, false },
	{ &KEY_NSPIRE_Z, 44, false },
	{ &KEY_NSPIRE_X, 45, false },
	{ &KEY_NSPIRE_C, 46, false },
	{ &KEY_NSPIRE_V, 47, false },
	{ &KEY_NSPIRE_B, 48, false },
	{ &KEY_NSPIRE_N, 49, false },
	{ &KEY_NSPIRE_M, 50, false },
	{ &KEY_NSPIRE_COMMA, 51, false },
	{ &KEY_NSPIRE_PERIOD, 52, false },
	{ &KEY_NSPIRE_DIVIDE, 53, false },
	{ &KEY_NSPIRE_VAR, KEY_SEMICOLON, false },
	{ &KEY_NSPIRE_SPACE, 57, false },
	{ &KEY_NSPIRE_HOME, 102, false },
	{ &KEY_NSPIRE_UP, 103, false },
	{ &KEY_NSPIRE_LEFT, 105, false },
	{ &KEY_NSPIRE_RIGHT, 106, false },
	{ &KEY_NSPIRE_DOWN, 108, false },
};

/* Derive guest time from CPU cycles so host stalls do not cause timer jumps. */
uint32_t get_uticks(void)
{
	return guest_ticks;
}

static void reset_guest_timer(void)
{
	guest_ticks = 0;
	last_cycle = 0;
	tick_remainder = 0;
}

static void advance_guest_timer(PC *pc)
{
	uint32_t cycle = (uint32_t)cpui386_get_cycle(pc->cpu);
	uint64_t scaled_time;
	uint64_t elapsed;
	uint32_t instructions;

	instructions = cycle - last_cycle;
	/* Keep the PIT advancing while the guest waits in HLT. */
	if (!instructions)
		instructions = IDLE_INS;
	last_cycle = cycle;
	scaled_time = (uint64_t)instructions *
	                INS_CYCLES *
	                1000000ULL + tick_remainder;
	elapsed = scaled_time / guest_hz;
	tick_remainder = scaled_time % guest_hz;
	guest_ticks += (uint32_t)elapsed;
}

/*
 * Reserve guest and VGA RAM before smaller allocations. bigmalloc() consumes
 * these blocks when pc_new() allocates emulator memory, avoiding Ndless heap
 * fragmentation during boot.
 */
void *bigmalloc(size_t size)
{
	void *memory;

	if (reserved_ram && size == reserved_ram_size) {
		memory = reserved_ram;
		reserved_ram = NULL;
		reserved_ram_size = 0;
		return memory;
	}
	if (reserved_vram && size == reserved_vram_size) {
		memory = reserved_vram;
		reserved_vram = NULL;
		reserved_vram_size = 0;
		return memory;
	}
	memory = calloc(1, size);
	if (!memory) {
		abort();
	}
	return memory;
}

static bool reserve_guest_memory(const PCConfig *config)
{
	/* Keep the configured RAM size; silently shrinking it can break the guest. */
	reserved_ram = calloc(1, config->mem_size);
	if (!reserved_ram)
		return false;
	reserved_ram_size = config->mem_size;
	return true;
}

static bool reserve_vga_memory(const PCConfig *config)
{
	reserved_vram = calloc(1, config->vga_mem_size);
	if (!reserved_vram) {
		return false;
	}
	reserved_vram_size = config->vga_mem_size;
	return true;
}

static void free_reserved_memory(void)
{
	free(reserved_ram);
	reserved_ram = NULL;
	reserved_ram_size = 0;
	free(reserved_vram);
	reserved_vram = NULL;
	reserved_vram_size = 0;
}

/* Run file checks before LCD takeover so TI-OS can display failures. */
static char boot_error[256];

static bool preflight_file(const char *label, const char *path, const char *mode,
			   long max_size)
{
	FILE *file;
	long file_size;

	if (!path || !path[0]) {
		snprintf(boot_error, sizeof(boot_error),
			 "Set %s in [pc] to a file path.", label);
		return false;
	}
	file = fopen(path, mode);
	if (!file) {
		snprintf(boot_error, sizeof(boot_error),
			 "Cannot open %s for %s:\n%s\nCheck the path and permissions.",
			 label, !strcmp(mode, "rb") ? "reading" : "reading and writing", path);
		return false;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0) {
		snprintf(boot_error, sizeof(boot_error),
			 "Empty or unreadable %s:\n%s.",
			 label, path);
		fclose(file);
		return false;
	}
	fclose(file);
	if (max_size > 0 && file_size > max_size) {
		snprintf(boot_error, sizeof(boot_error),
			 "%s exceeds the %ld-byte limit:\n%s", label, max_size, path);
		return false;
	}
	return true;
}

static bool preflight_boot_files(const PCConfig *config)
{
	unsigned int index;

	if (!preflight_file("bios", config->bios, "rb", 0x100000L))
		return false;
	if (!preflight_file("vga_bios", config->vga_bios, "rb",
			    config->mem_size - 0xc0000L))
		return false;
	if ((!config->disks[0] || !config->disks[0][0]) &&
	    (!config->fdd[0] || !config->fdd[0][0])) {
		snprintf(boot_error, sizeof(boot_error),
			 "Set hda or fda in [pc] to your boot disk image.");
		return false;
	}
	for (index = 0;
	     index < sizeof(config->disks) / sizeof(config->disks[0]); index++) {
		if (config->disks[index] && config->disks[index][0] &&
		    !preflight_file("disk image", config->disks[index], "r+b", 0))
			return false;
	}
	for (index = 0;
	     index < sizeof(config->fdd) / sizeof(config->fdd[0]); index++) {
		if (config->fdd[index] && config->fdd[index][0] &&
		    !preflight_file("floppy image", config->fdd[index], "r+b", 0))
			return false;
	}
	return true;
}

int load_rom(void *guest_memory, const char *path, uword address, int backward)
{
	FILE *file = fopen(path, "rb");
	long file_size;
	size_t bytes_read;

	if (!file) {
		abort();
	}
	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0) {
		fclose(file);
		abort();
	}
	rewind(file);
	if (backward)
		bytes_read = fread((uint8_t *)guest_memory + address - file_size, 1, file_size, file);
	else
		bytes_read = fread((uint8_t *)guest_memory + address, 1, file_size, file);
	fclose(file);
	if (bytes_read != (size_t)file_size) {
		abort();
	}
	return (int)file_size;
}

static uint64_t host_millis(void)
{
	return (uint64_t)(((unsigned long)clock() * 1000UL) / CLOCKS_PER_SEC);
}

/* main() checks for CX II before any LCD register access. */
static void disable_os_cursor(void)
{
	volatile uint32_t *cursor_reg =
		(volatile uint32_t *)CURSOR_REG;
	*cursor_reg &= ~1U;
}

void vga_mode_changed(void)
{
	mode_changed = true;
}

/*
 * Ndless can return LCD ownership to TI-OS during launch and modal transitions.
 * Reclaim it and disable the TI-OS cursor overlay before drawing.
 */
static bool claim_lcd(Display *display,
		bool force, uint64_t now, bool *reclaimed)
{
	if (force || !display->lcd_active ||
	    !display->last_claim_ms ||
	    now - display->last_claim_ms >=
		    LCD_RETRY_MS) {
		disable_os_cursor();
		display->lcd_active = lcd_init(SCR_320x240_565);
		display->last_claim_ms = now;
		if (reclaimed)
			*reclaimed = display->lcd_active;
	} else {
		disable_os_cursor();
		if (reclaimed)
			*reclaimed = false;
	}
	return display->lcd_active;
}

static void draw_frame(Display *display, bool force)
{
	uint64_t now = host_millis();

	if (!display->ready)
		return;
	if (!claim_lcd(display, force, now, NULL))
		return;
	lcd_blit(display->framebuffer, SCR_320x240_565);
	display->last_draw_ms = now;
}

static void draw_region(Display *display,
		int left, int top, int width, int height)
{
	uint64_t now = host_millis();
	uint16_t *source;
	uint16_t *screen;
	int row;

	/* The redraw queue clips bounds before this function copies any pixels. */
	if (width * height >= REDRAW_AREA) {
		draw_frame(display, false);
		return;
	}
	if (!claim_lcd(display, false, now, NULL))
		return;
	source = (uint16_t *)display->framebuffer +
		top * SCREEN_WIDTH + left;
	screen = (uint16_t *)REAL_SCREEN_BASE_ADDRESS +
		top * SCREEN_WIDTH + left;
	if (width == SCREEN_WIDTH) {
		memcpy(screen, source,
			(size_t)height * SCREEN_WIDTH * sizeof(uint16_t));
		display->last_draw_ms = now;
		return;
	}
	for (row = 0; row < height; row++) {
		memcpy(screen, source, (size_t)width * sizeof(uint16_t));
		source += SCREEN_WIDTH;
		screen += SCREEN_WIDTH;
	}
	display->last_draw_ms = now;
}

static void queue_redraw(Display *display,
		int left, int top, int width, int height)
{
	int right;
	int bottom;

	if (!display->ready)
		return;
	if (left <= 0 && top <= 0 && width >= SCREEN_WIDTH &&
	    height >= SCREEN_HEIGHT) {
		display->full_redraw = true;
		display->dirty_x = 0;
		display->dirty_y = 0;
		display->dirty_width = SCREEN_WIDTH;
		display->dirty_height = SCREEN_HEIGHT;
	} else {
		if (left < 0) {
			width += left;
			left = 0;
		}
		if (top < 0) {
			height += top;
			top = 0;
		}
		if (left >= SCREEN_WIDTH ||
		    top >= SCREEN_HEIGHT ||
		    width <= 0 || height <= 0)
			return;
		if (left + width > SCREEN_WIDTH)
			width = SCREEN_WIDTH - left;
		if (top + height > SCREEN_HEIGHT)
			height = SCREEN_HEIGHT - top;
		if (display->full_redraw)
			return;
		if (!display->dirty) {
			display->dirty_x = left;
			display->dirty_y = top;
			display->dirty_width = width;
			display->dirty_height = height;
		} else {
			right = display->dirty_x + display->dirty_width;
			bottom = display->dirty_y + display->dirty_height;
			if (left < display->dirty_x)
				display->dirty_x = left;
			if (top < display->dirty_y)
				display->dirty_y = top;
			if (left + width > right)
				right = left + width;
			if (top + height > bottom)
				bottom = top + height;
			display->dirty_width = right - display->dirty_x;
			display->dirty_height = bottom - display->dirty_y;
		}
	}
	display->dirty = true;
}

/*
 * Merge VGA callbacks into one LCD update per video poll.
 */
static void flush_redraw(Display *display)
{
	if (!display->dirty)
		return;
	display->dirty = false;
	if (display->full_redraw)
		draw_frame(display, false);
	else
		draw_region(display, display->dirty_x,
				       display->dirty_y, display->dirty_width,
				       display->dirty_height);
	display->full_redraw = false;
}

static void keep_lcd(Display *display)
{
	uint64_t now = host_millis();
	bool reclaimed = false;

	if (mode_changed)
		return;
	if (!display->last_draw_ms ||
	    now - display->last_draw_ms >=
		    LCD_POLL_MS) {
		if (claim_lcd(display, false, now,
					    &reclaimed) &&
		    reclaimed) {
			lcd_blit(display->framebuffer, SCR_320x240_565);
			display->last_draw_ms = now;
		}
	}
}

static void redraw(void *context,
		int left, int top, int width, int height)
{
	Display *display = context;

	/* Take LCD ownership on the first VGA redraw, regardless of firmware. */
	if (!display->ready) {
		display->ready = true;
		mode_changed = false;
		draw_frame(display, true);
	} else if (mode_changed) {
		mode_changed = false;
		queue_redraw(display, 0, 0, SCREEN_WIDTH,
				     SCREEN_HEIGHT);
	} else {
		queue_redraw(display, left, top, width, height);
	}
}

static uint32_t hide_os_cursor(void)
{
	volatile uint32_t *cursor_reg =
		(volatile uint32_t *)CURSOR_REG;
	uint32_t saved_cursor = *cursor_reg;

	*cursor_reg = saved_cursor & ~1U;
	return saved_cursor;
}

static void restore_os_cursor(uint32_t saved_cursor)
{
	volatile uint32_t *cursor_reg =
		(volatile uint32_t *)CURSOR_REG;
	*cursor_reg = saved_cursor;
}

static int clamp_int(int value, int min_value, int max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

static int scale_mouse_delta(int delta, int *remainder)
{
	int sign = delta < 0 ? -1 : 1;
	int magnitude = delta < 0 ? -delta : delta;
	int accelerated;
	int accumulated;
	int scaled;

	if (magnitude <= PAD_DEADZONE)
		return 0;
	magnitude -= PAD_DEADZONE;
	accelerated = magnitude;
	if (magnitude >= PAD_ACCEL_AT)
		accelerated += (magnitude * PAD_SCALE) /
				PAD_ACCEL_SCALE;
	accumulated = *remainder + sign * accelerated;
	scaled = accumulated / PAD_SCALE;
	*remainder = accumulated % PAD_SCALE;
	return clamp_int(scaled, -MOUSE_DELTA_MAX, MOUSE_DELTA_MAX);
}

static int touchpad_arrow_mask(unsigned char arrow)
{
	switch (arrow) {
	case TPAD_ARROW_UP: return TOUCH_ARROW_UP;
	case TPAD_ARROW_UPRIGHT: return TOUCH_ARROW_UP | TOUCH_ARROW_RIGHT;
	case TPAD_ARROW_RIGHT: return TOUCH_ARROW_RIGHT;
	case TPAD_ARROW_RIGHTDOWN: return TOUCH_ARROW_RIGHT | TOUCH_ARROW_DOWN;
	case TPAD_ARROW_DOWN: return TOUCH_ARROW_DOWN;
	case TPAD_ARROW_DOWNLEFT: return TOUCH_ARROW_DOWN | TOUCH_ARROW_LEFT;
	case TPAD_ARROW_LEFT: return TOUCH_ARROW_LEFT;
	case TPAD_ARROW_LEFTUP: return TOUCH_ARROW_LEFT | TOUCH_ARROW_UP;
	default: return 0;
	}
}

static void update_touch_arrows(PC *pc, int new_arrows)
{
	static const struct {
		int mask;
		int keycode;
	} arrow_keys[] = {
		{ TOUCH_ARROW_UP, 103 },
		{ TOUCH_ARROW_DOWN, 108 },
		{ TOUCH_ARROW_LEFT, 105 },
		{ TOUCH_ARROW_RIGHT, 106 },
	};
	unsigned int index;

	for (index = 0;
	     index < sizeof(arrow_keys) / sizeof(arrow_keys[0]); index++) {
		bool was_pressed = (touchpad_state.arrows & arrow_keys[index].mask) != 0;
		bool is_pressed = (new_arrows & arrow_keys[index].mask) != 0;

		if (was_pressed != is_pressed)
			ps2_put_keycode(pc->kbd, is_pressed, arrow_keys[index].keycode);
	}
	touchpad_state.arrows = new_arrows;
}

/*
 * Convert the calculator touchpad into PS/2 relative mouse packets. Fractional
 * remainders make slow drags smooth, while the acceleration threshold still
 * allows full-screen movement without excessive finger travel.
 */
static void poll_touchpad_mouse(PC *pc)
{
	touchpad_report_t report;
	bool touching;
	int buttons = 0;
	int arrows = 0;
	int dx = 0;
	int dy = 0;

	if (!is_touchpad || !pc->mouse)
		return;
	/* Release held inputs after a failed scan so keys and clicks cannot stick. */
	if (touchpad_scan(&report) != 0) {
		update_touch_arrows(pc, 0);
		if (touchpad_state.buttons) {
			ps2_mouse_event(pc->mouse, 0, 0, 0, 0);
			touchpad_state.buttons = 0;
		}
		touchpad_state.has_position = false;
		touchpad_state.x_remainder = 0;
		touchpad_state.y_remainder = 0;
		return;
	}
	touching = report.contact || report.pressed;
	if (report.pressed)
		arrows = touchpad_arrow_mask(report.arrow);
	if (report.pressed && !arrows)
		buttons |= isKeyPressed(KEY_NSPIRE_CTRL) ?
			BTN_RIGHT : BTN_LEFT;
	update_touch_arrows(pc, arrows);
	if (touching && touchpad_state.has_position) {
		dx = scale_mouse_delta((int)report.x - (int)touchpad_state.last_x,
				       &touchpad_state.x_remainder);
		dy = scale_mouse_delta((int)touchpad_state.last_y - (int)report.y,
				       &touchpad_state.y_remainder);
	}
	if (touching) {
		touchpad_state.last_x = report.x;
		touchpad_state.last_y = report.y;
		touchpad_state.has_position = true;
	} else {
		touchpad_state.has_position = false;
		touchpad_state.x_remainder = 0;
		touchpad_state.y_remainder = 0;
	}
	if (dx || dy || buttons != touchpad_state.buttons) {
		ps2_mouse_event(pc->mouse, dx, dy, 0, buttons);
		touchpad_state.buttons = buttons;
	}
}

static void poll_keys(PC *pc)
{
	unsigned int index;

	for (index = 0;
	     index < sizeof(keys) / sizeof(keys[0]); index++) {
		bool is_pressed;

		/* Touchpad arrow zones are reported separately by poll_touchpad_mouse. */
		if (is_touchpad &&
		    keys[index].key->tpad_arrow != TPAD_ARROW_NONE)
			continue;
		is_pressed = isKeyPressed(*keys[index].key);
		if (is_pressed == keys[index].is_pressed)
			continue;
		keys[index].is_pressed = is_pressed;
		ps2_put_keycode(pc->kbd, is_pressed,
			       keys[index].keycode);
	}
}

static void configure_defaults(PCConfig *config)
{
	memset(config, 0, sizeof(*config));
	config->mem_size = 16 * 1024 * 1024;
	config->vga_mem_size = 256 * 1024;
	config->width = SCREEN_WIDTH;
	config->height = SCREEN_HEIGHT;
	config->cpu_gen = 4;
	config->fpu = 0;
	config->clock_hz = CPU_HZ;
	config->vga_force_8dm = 1;
}

static void free_config_paths(PCConfig *config)
{
	unsigned int index;

	free((void *)config->linuxstart);
	free((void *)config->kernel);
	free((void *)config->initrd);
	free((void *)config->cmdline);
	free((void *)config->bios);
	free((void *)config->vga_bios);
	for (index = 0;
	     index < sizeof(config->disks) / sizeof(config->disks[0]); index++) {
		free((void *)config->disks[index]);
	}
	for (index = 0;
	     index < sizeof(config->fdd) / sizeof(config->fdd[0]); index++) {
		free((void *)config->fdd[index]);
	}
}

static int startup_error(PCConfig *config, const char *message)
{
	free_config_paths(config);
	free_reserved_memory();
	refresh_osscr();
	show_msgbox("WiNspire", message);
	return 1;
}

static void reset_input_state(void)
{
	unsigned int index;

	for (index = 0;
	     index < sizeof(keys) / sizeof(keys[0]); index++)
		keys[index].is_pressed = false;
	memset(&touchpad_state, 0, sizeof(touchpad_state));
}

int main(int argc, char **argv)
{
	const char *config_path = argc > 1 ? argv[1] : "winspire.ini.tns";
	PCConfig config;
	Display display;
	PC *pc;
	uint32_t loops = 0;
	bool first_step_done = false;
	uint32_t saved_cursor = 0;
	scr_type_t screen_format;
	int error;

	assert_ndless_rev(2004);
	enable_relative_paths(argv);
	free_reserved_memory();
	reset_input_state();
	mode_changed = false;
	screen_format = lcd_type();
	if (!is_cx2) {
		refresh_osscr();
		show_msgbox("WiNspire", "This build targets the CX II.");
		return 1;
	}
	if (screen_format != SCR_320x240_565) {
		refresh_osscr();
		show_msgbox("WiNspire", "Unsupported LCD layout.");
		return 1;
	}

	configure_defaults(&config);
	input_poll_loops = INPUT_POLL_LOOPS;
	video_poll_loops = VIDEO_POLL_LOOPS;
	error = ini_parse(config_path, parse_native_config, &config);
	if (error) {
		if (error > 0)
			snprintf(boot_error, sizeof(boot_error),
				 "Invalid INI entry at line %d:\n%s", error, config_path);
		else if (error == -1)
			snprintf(boot_error, sizeof(boot_error),
				 "Cannot open config:\n%s\nCheck the path and read permissions.",
				 config_path);
		else
			snprintf(boot_error, sizeof(boot_error),
				 "Not enough free RAM to read config:\n%s", config_path);
		startup_error(&config, boot_error);
		return error;
	}
	config.width = SCREEN_WIDTH;
	config.height = SCREEN_HEIGHT;
	config.enable_serial = 0;
	config.vga_force_8dm = 1;
	if (config.clock_hz < CPU_HZ_MIN || config.clock_hz > CPU_HZ_MAX) {
		snprintf(boot_error, sizeof(boot_error),
			 "clock_hz in [cpu] must be between %u and %u in %s.",
			 CPU_HZ_MIN, CPU_HZ_MAX, config_path);
		return startup_error(&config, boot_error);
	}
	if (!is_power_of_two(input_poll_loops) ||
	    !is_power_of_two(video_poll_loops) ||
	    input_poll_loops > POLL_MAX ||
	    video_poll_loops > POLL_MAX) {
		snprintf(boot_error, sizeof(boot_error),
			 "input_poll_loops and video_poll_loops in [nspire] must be "
			 "powers of two from 1 to %u.\n%s", POLL_MAX, config_path);
		return startup_error(&config, boot_error);
	}
	guest_hz = config.clock_hz;
	if (config.mem_size < GUEST_RAM_MIN || config.mem_size > GUEST_RAM_MAX) {
		snprintf(boot_error, sizeof(boot_error),
			 "mem_size must be between %ldM and %ldM in %s.",
			 GUEST_RAM_MIN / (1024L * 1024), GUEST_RAM_MAX / (1024L * 1024),
			 config_path);
		return startup_error(&config, boot_error);
	}
	if (config.vga_mem_size < VGA_RAM_MIN || config.vga_mem_size > VGA_RAM_MAX) {
		snprintf(boot_error, sizeof(boot_error),
			 "vga_mem_size must be between %ldK and %ldK in %s.",
			 VGA_RAM_MIN / 1024L, VGA_RAM_MAX / 1024L, config_path);
		return startup_error(&config, boot_error);
	}
	if (!preflight_boot_files(&config))
		return startup_error(&config, boot_error);
	if (!reserve_guest_memory(&config)) {
		snprintf(boot_error, sizeof(boot_error),
			 "Could not allocate %ld KiB of guest RAM.\n"
			 "Lower mem_size in %s.", config.mem_size / 1024L, config_path);
		return startup_error(&config, boot_error);
	}
	if (!reserve_vga_memory(&config)) {
		snprintf(boot_error, sizeof(boot_error),
			 "Could not allocate %ld KiB of video memory.\n"
			 "Lower mem_size or vga_mem_size in %s.",
			 config.vga_mem_size / 1024L, config_path);
		return startup_error(&config, boot_error);
	}

	memset(&display, 0, sizeof(display));
	display.framebuffer = calloc(1, FRAMEBUFFER_BYTES);
	if (!display.framebuffer) {
		snprintf(boot_error, sizeof(boot_error),
			 "Not enough free RAM for the calculator display.\n"
			 "Lower mem_size in %s.", config_path);
		return startup_error(&config, boot_error);
	}
	reset_guest_timer();
	pc = pc_new(redraw, &display, display.framebuffer, &config);
	load_bios_and_reset(pc);
	saved_cursor = hide_os_cursor();
	vga_refresh(pc->vga, redraw, &display, 1);
	reset_guest_timer();
	i8254_rebase(pc->pit);
	pc->boot_start_time = get_uticks();

	while (pc->shutdown_state != 8 && !on_key_pressed()) {
		pc_step(pc);
		advance_guest_timer(pc);
		if (!first_step_done) {
			if (display.ready) {
				draw_frame(&display, true);
			}
			first_step_done = true;
		}
		loops++;
		if ((loops & (input_poll_loops - 1)) == 0) {
			poll_keys(pc);
			poll_touchpad_mouse(pc);
		}
		if ((loops & (video_poll_loops - 1)) == 0) {
			pc_vga_step(pc);
			flush_redraw(&display);
			if (display.ready)
				keep_lcd(&display);
		}
	}
	if (on_key_pressed())
		wait_no_key_pressed();
	lcd_init(SCR_TYPE_INVALID);
	restore_os_cursor(saved_cursor);
	refresh_osscr();
	pc_free_buffers(pc);
	free(display.framebuffer);
	free_config_paths(&config);
	free_reserved_memory();
	return 0;
}
