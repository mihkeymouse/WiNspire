// tiny386 calculator frontend.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <signal.h>
#include "pc.h"

#ifdef CALC_PROFILE_SELECT
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>
#endif

#ifdef CALC_PROFILE_SELECT
#define CALC_CONFIG_MAX 16
#define CALC_CONFIG_PATH_MAX 256

typedef struct ConfigEntry {
	char name[64];
	char path[CALC_CONFIG_PATH_MAX];
} ConfigEntry;

static int config_order(const char *name)
{
	if (strcmp(name, "win9x") == 0)
		return 0;
	if (strcmp(name, "win2000") == 0)
		return 1;
	if (strcmp(name, "xpe") == 0)
		return 2;
	return 100;
}

static const char *config_label(const char *name)
{
	if (strcmp(name, "win9x") == 0)
		return "Windows 95 / 98 / Me";
	if (strcmp(name, "win2000") == 0)
		return "Windows 2000";
	if (strcmp(name, "xpe") == 0)
		return "Windows XP Embedded";
	return name;
}

static int config_compare(const void *a, const void *b)
{
	const ConfigEntry *ea = a;
	const ConfigEntry *eb = b;
	int order_a = config_order(ea->name);
	int order_b = config_order(eb->name);

	if (order_a != order_b)
		return order_a - order_b;
	return strcmp(ea->name, eb->name);
}

static int config_is_ini(const char *name)
{
	size_t len = strlen(name);

	return len > 4 && strcmp(name + len - 4, ".ini") == 0;
}

static int config_scan(const char *dir, ConfigEntry *entries)
{
	DIR *dp;
	struct dirent *de;
	int count = 0;

	dp = opendir(dir);
	if (!dp)
		return -1;
	while ((de = readdir(dp)) != NULL && count < CALC_CONFIG_MAX) {
		struct stat st;
		size_t name_len;

		if (!config_is_ini(de->d_name))
			continue;
		if (snprintf(entries[count].path, sizeof(entries[count].path),
		             "%s/%s", dir, de->d_name) >=
		    (int)sizeof(entries[count].path))
			continue;
		if (stat(entries[count].path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		name_len = strlen(de->d_name) - 4;
		if (name_len >= sizeof(entries[count].name))
			name_len = sizeof(entries[count].name) - 1;
		memcpy(entries[count].name, de->d_name, name_len);
		entries[count].name[name_len] = '\0';
		count++;
	}
	closedir(dp);
	qsort(entries, (size_t)count, sizeof(entries[0]), config_compare);
	return count;
}

static int config_find(const ConfigEntry *entries, int count,
			    const char *value)
{
	char *end;
	long number;
	int i;

	if (!value || !*value)
		return -1;
	number = strtol(value, &end, 10);
	if (*end == '\0' && number >= 1 && number <= count)
		return (int)number - 1;
	for (i = 0; i < count; i++) {
		const char *base = strrchr(entries[i].path, '/');

		base = base ? base + 1 : entries[i].path;
		if (strcmp(value, entries[i].name) == 0 || strcmp(value, base) == 0)
			return i;
	}
	return -1;
}

static void console_write(int fd, const char *text)
{
	if (fd >= 0) {
		ssize_t ignored = write(fd, text, strlen(text));

		(void)ignored;
	}
	fputs(text, stderr);
}

static int config_select(char *selected, size_t selected_size)
{
	const char *dir = getenv("WINSPIRE_CONFIG_DIR");
	const char *requested = getenv("WINSPIRE_PROFILE");
	ConfigEntry entries[CALC_CONFIG_MAX];
	char menu[1024];
	char input[32];
	int count, selected_index;
	int console_fd = -1;
	int menu_len = 0;
	int input_len = 0;
	int i;

	if (!dir || !*dir)
		dir = "/configs";
	count = config_scan(dir, entries);
	if (count <= 0) {
		fprintf(stderr, " no .ini files in %s\n", dir);
		return -1;
	}
	selected_index = config_find(entries, count, requested);
	if (selected_index < 0 && requested && *requested)
		fprintf(stderr, "unknown WINSPIRE_PROFILE=%s\n", requested);
	if (selected_index < 0) {
		const char *console_paths[] = {
			"/dev/tty0", "/dev/tty1", "/dev/console", NULL
		};

		for (i = 0; console_paths[i]; i++) {
			console_fd = open(console_paths[i], O_RDWR | O_NONBLOCK);
			if (console_fd >= 0)
				break;
		}
	}
	/* Keep this menu before fbmirror_open() changes the LCD presentation. */
	menu_len += snprintf(menu + menu_len, sizeof(menu) - (size_t)menu_len,
			     "\n[WiNspire] Choose Windows version:\n");
	for (i = 0; i < count && menu_len < (int)sizeof(menu); i++)
		menu_len += snprintf(menu + menu_len, sizeof(menu) - (size_t)menu_len,
				     "  %d. %s\n", i + 1,
				     config_label(entries[i].name));
	if (selected_index < 0 && console_fd >= 0)
		menu_len += snprintf(menu + menu_len, sizeof(menu) - (size_t)menu_len,
				     "Press number and Enter.\n");
	console_write(console_fd, menu);

	while (selected_index < 0 && console_fd >= 0) {
		struct pollfd pfd;
		char chunk[16];
		int rc;

		pfd.fd = console_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		rc = poll(&pfd, 1, 100);
		if (rc <= 0 || !(pfd.revents & POLLIN))
			continue;
		rc = (int)read(console_fd, chunk, sizeof(chunk));
		for (i = 0; i < rc; i++) {
			if (isdigit((unsigned char)chunk[i]) &&
			    input_len < (int)sizeof(input) - 1)
				input[input_len++] = chunk[i];
			if (chunk[i] == '\n' || chunk[i] == '\r') {
				input[input_len] = '\0';
				selected_index = config_find(entries, count, input);
				input_len = 0;
			}
		}
	}
	if (console_fd >= 0)
		close(console_fd);
	if (selected_index < 0)
		return -1;
	if (snprintf(selected, selected_size, "%s", entries[selected_index].path) >=
	    (int)selected_size)
		return -1;
	setenv("WINSPIRE_PROFILE", entries[selected_index].name, 1);
	fprintf(stderr, "profile-select: %s (%s)\n",
		entries[selected_index].name, selected);
	return 0;
}
#endif

#ifdef TINY386_HEADLESS_DIAG
#define E_RT "TINY386_HEADLESS_REALTIME"
#define E_SCALE "TINY386_GUEST_CYCLE_SCALE"
#define E_IDLE "TINY386_IDLE_ASSIST"
#define E_MOVS "TINY386_BULK_REP_RAM"
#define E_STOS "TINY386_BULK_REP_STOS"
#define E_VGA "TINY386_HEADLESS_VGA_EVERY"
#define E_VGA_FULL "TINY386_HEADLESS_VGA_FULL_EVERY"
#define E_INPUT "TINY386_HEADLESS_VGA_INPUT_EVERY"
#define E_BURST "TINY386_HEADLESS_VGA_INPUT_BURST"
#define E_TEXT "TINY386_VGA_TEXT_TRANSITION_HOLD"
#define E_2K_FAST "TINY386_WIN2K_BOOT_FAST"

static void set_default(const char *name, const char *value)
{
	/* Keep existing overrides. */
	setenv(name, value, 0);
}

static const char *profile_name(const char *config_path)
{
	const char *requested = getenv("WINSPIRE_PROFILE");
	const char *base;

	if (requested && *requested)
		return requested;
	base = strrchr(config_path, '/');
	return base ? base + 1 : config_path;
}

static void profile_apply(const char *config_path)
{
	const char *profile = profile_name(config_path);

	if (strncmp(profile, "win2000", 7) == 0) {
		set_default(E_RT, "1");
		set_default(E_SCALE, "1");
		set_default(E_IDLE, "0");
		set_default(E_MOVS, "0");
		set_default(E_STOS, "1");
		set_default(E_VGA, "512");
		set_default(E_VGA_FULL, "0");
		set_default(E_INPUT, "4");
		set_default(E_BURST, "256");
		set_default(E_TEXT, "0");
		set_default(E_2K_FAST, "1");
	} else if (strncmp(profile, "xpe", 3) == 0) {
		set_default(E_RT, "0");
		set_default(E_SCALE, "1");
		set_default(E_IDLE, "1");
		set_default(E_MOVS, "1");
		set_default(E_STOS, "1");
		set_default(E_VGA, "2048");
		set_default(E_VGA_FULL, "2048");
		set_default(E_INPUT, "8");
		set_default(E_BURST, "128");
		set_default(E_TEXT, "0");
		set_default(E_2K_FAST, "0");
	} else if (strncmp(profile, "win9x", 5) == 0) {
		set_default(E_RT, "0");
		set_default(E_SCALE, "12");
		set_default(E_IDLE, "0");
		set_default(E_MOVS, "0");
		set_default(E_STOS, "0");
		/* Match native refresh timing. */
		set_default(E_VGA, "8");
		set_default(E_VGA_FULL, "0");
		/* Refresh immediately after input. */
		set_default(E_INPUT, "1");
		set_default(E_BURST, "256");
		/* Ignore transient Win9x text-mode switches. */
		set_default(E_TEXT, "255");
		set_default(E_2K_FAST, "0");
	}
	fprintf(stderr,
		"profile-policy: %s realtime=%s scale=%s bulk=%s stos=%s input=%s/%s text-hold=%s boot-fast=%s\n",
		profile,
		getenv(E_RT) ?: "default",
		getenv(E_SCALE) ?: "default",
		getenv(E_MOVS) ?: "default",
		getenv(E_STOS) ?: "default",
		getenv(E_INPUT) ?: "default",
		getenv(E_BURST) ?: "default",
		getenv(E_TEXT) ?: "default",
		getenv(E_2K_FAST) ?: "default");
}
#endif

#if defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
#define LINUX_INPUT_MAX_DEVS 16
#define LINUX_INPUT_MAX_KEYMAP 32
#define LINUX_TOUCH_DEADZONE 2
#define LINUX_TOUCH_DIVISOR 18
#define LINUX_TOUCH_FAST_THRESHOLD 80
#define LINUX_TOUCH_FAST_DIVISOR 26
#define LINUX_TOUCH_MAX_DELTA 96

typedef struct KeyMapEntry {
	unsigned short from;
	unsigned short to;
} KeyMapEntry;

static KeyMapEntry linux_keymap[LINUX_INPUT_MAX_KEYMAP];
static int linux_keymap_count;

static void input_parse_keymap(void)
{
	const char *p = getenv("TINY386_KEYMAP");

	linux_keymap_count = 0;
	while (p && *p && linux_keymap_count < LINUX_INPUT_MAX_KEYMAP) {
		char *end;
		unsigned long from = strtoul(p, &end, 0);
		unsigned long to;

		if (end == p || (*end != ':' && *end != '='))
			break;
		p = end + 1;
		to = strtoul(p, &end, 0);
		if (end == p || from > 0xffffUL || to > 0xffffUL)
			break;
		linux_keymap[linux_keymap_count].from = (unsigned short)from;
		linux_keymap[linux_keymap_count].to = (unsigned short)to;
		linux_keymap_count++;
		p = end;
		while (*p == ',' || *p == ';' || *p == ' ' || *p == '\t')
			p++;
	}
}

typedef struct ConsoleOwner {
	int fd;
	int graphics_active;
} ConsoleOwner;

typedef struct InputBridge {
	int fds[LINUX_INPUT_MAX_DEVS];
	int count;
	int buttons;
	int dx;
	int dy;
	int dz;
	int mouse_dirty;
	int sensitivity;
	int abs_x[LINUX_INPUT_MAX_DEVS];
	int abs_y[LINUX_INPUT_MAX_DEVS];
	int abs_pending_x[LINUX_INPUT_MAX_DEVS];
	int abs_pending_y[LINUX_INPUT_MAX_DEVS];
	int abs_rem_x[LINUX_INPUT_MAX_DEVS];
	int abs_rem_y[LINUX_INPUT_MAX_DEVS];
	int mt_slot[LINUX_INPUT_MAX_DEVS];
	unsigned char abs_x_valid[LINUX_INPUT_MAX_DEVS];
	unsigned char abs_y_valid[LINUX_INPUT_MAX_DEVS];
	unsigned char abs_pending_x_valid[LINUX_INPUT_MAX_DEVS];
	unsigned char abs_pending_y_valid[LINUX_INPUT_MAX_DEVS];
	unsigned char contact[LINUX_INPUT_MAX_DEVS];
	unsigned char contact_seen[LINUX_INPUT_MAX_DEVS];
	struct input_absinfo abs_x_info[LINUX_INPUT_MAX_DEVS];
	struct input_absinfo abs_y_info[LINUX_INPUT_MAX_DEVS];
} InputBridge;

static void linux_console_init(ConsoleOwner *con)
{
	const char *paths[] = { "/dev/tty0", "/dev/console", NULL };
	int i;

	memset(con, 0, sizeof(*con));
	con->fd = -1;
	for (i = 0; paths[i]; i++) {
		con->fd = open(paths[i], O_RDWR | O_NONBLOCK);
		if (con->fd >= 0)
			break;
	}
	if (con->fd < 0) {
		fprintf(stderr, "linux-console: no console device for graphics mode: %s\n",
			strerror(errno));
		return;
	}
	if (ioctl(con->fd, KDSETMODE, KD_GRAPHICS) == 0) {
		con->graphics_active = 1;
		fprintf(stderr, "linux-console: graphics mode active\n");
	} else {
		fprintf(stderr, "linux-console: KD_GRAPHICS failed: %s\n",
			strerror(errno));
	}
}

static void linux_console_close(ConsoleOwner *con)
{
	if (!con)
		return;
	if (con->fd >= 0) {
		if (con->graphics_active)
			ioctl(con->fd, KDSETMODE, KD_TEXT);
		close(con->fd);
		con->fd = -1;
		con->graphics_active = 0;
	}
}

static void linux_input_init(InputBridge *in)
{
	int i;
	const char *sensitivity;

	memset(in, 0, sizeof(*in));
	in->sensitivity = 100;
	sensitivity = getenv("TINY386_TOUCHPAD_SENSITIVITY");
	if (sensitivity && *sensitivity) {
		long value = strtol(sensitivity, NULL, 0);

		if (value >= 25 && value <= 400)
			in->sensitivity = (int)value;
	}
	for (i = 0; i < LINUX_INPUT_MAX_DEVS; i++) {
		char path[64];
		char name[128];
		int fd;

		in->fds[i] = -1;
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGRAB, 1) < 0)
			fprintf(stderr, "linux-input: warning: cannot grab %s: %s\n",
				path, strerror(errno));
		in->fds[in->count] = fd;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
			strcpy(name, "unknown");
		if ((ioctl(fd, EVIOCGABS(ABS_X),
			   &in->abs_x_info[in->count]) == 0 &&
		     ioctl(fd, EVIOCGABS(ABS_Y),
			   &in->abs_y_info[in->count]) == 0) ||
		    (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X),
			   &in->abs_x_info[in->count]) == 0 &&
		     ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y),
			   &in->abs_y_info[in->count]) == 0)) {
			fprintf(stderr,
				"linux-input: %s (%s) absolute x=%d..%d y=%d..%d\n",
				path, name,
				in->abs_x_info[in->count].minimum,
				in->abs_x_info[in->count].maximum,
				in->abs_y_info[in->count].minimum,
				in->abs_y_info[in->count].maximum);
		} else {
			fprintf(stderr, "linux-input: %s (%s) relative/key device\n",
				path, name);
		}
		in->count++;
		if (in->count >= LINUX_INPUT_MAX_DEVS)
			break;
	}
	input_parse_keymap();
	fprintf(stderr,
		"linux-input: ready devices=%d sensitivity=%d%% keymaps=%d\n",
		in->count, in->sensitivity, linux_keymap_count);
	if (!in->count)
		fprintf(stderr, "linux-input: no /dev/input/event* devices opened\n");
}

static void linux_input_close(InputBridge *in)
{
	int i;

	for (i = 0; i < in->count; i++) {
		if (in->fds[i] >= 0) {
			ioctl(in->fds[i], EVIOCGRAB, 0);
			close(in->fds[i]);
			in->fds[i] = -1;
		}
	}
	in->count = 0;
}

static void linux_input_key(PC *pc, unsigned short code, int down)
{
	int i;

	if (!pc || !pc->kbd)
		return;
	if (code == KEY_POWER || code == KEY_SLEEP)
		return;
	for (i = 0; i < linux_keymap_count; i++) {
		if (linux_keymap[i].from == code) {
			code = linux_keymap[i].to;
			break;
		}
	}
	if (!code)
		return;
	ps2_put_keycode(pc->kbd, down, code);
}

static void linux_input_button(InputBridge *in, unsigned short code,
			       int down)
{
	int mask = 0;

	if (code == BTN_LEFT)
		mask = 1;
	else if (code == BTN_RIGHT)
		mask = 2;
	else if (code == BTN_MIDDLE)
		mask = 4;
	if (!mask)
		return;
	if (down)
		in->buttons |= mask;
	else
		in->buttons &= ~mask;
	in->mouse_dirty = 1;
}

static void input_send_mouse(InputBridge *in, PC *pc)
{
	if (!in || !pc || !pc->mouse || !in->mouse_dirty)
		return;
	ps2_mouse_event(pc->mouse, in->dx, in->dy, in->dz, in->buttons);
	in->dx = 0;
	in->dy = 0;
	in->dz = 0;
	in->mouse_dirty = 0;
}

static int input_abs_delta(int value, int *last,
				  unsigned char *valid, int *remainder,
				  int sensitivity, int invert)
{
	long delta, magnitude, contribution, accumulated, scaled;

	if (!*valid) {
		*last = value;
		*valid = 1;
		return 0;
	}
	delta = invert ? (long)*last - value : (long)value - *last;
	*last = value;
	magnitude = delta < 0 ? -delta : delta;
	if (magnitude <= LINUX_TOUCH_DEADZONE)
		return 0;
	magnitude -= LINUX_TOUCH_DEADZONE;
	contribution = magnitude;
	if (magnitude >= LINUX_TOUCH_FAST_THRESHOLD)
		contribution += magnitude * LINUX_TOUCH_DIVISOR /
			LINUX_TOUCH_FAST_DIVISOR;
	contribution = contribution * sensitivity / 100L;
	accumulated = *remainder + (delta < 0 ? -contribution : contribution);
	scaled = accumulated / LINUX_TOUCH_DIVISOR;
	*remainder = (int)(accumulated % LINUX_TOUCH_DIVISOR);
	if (scaled < -LINUX_TOUCH_MAX_DELTA)
		scaled = -LINUX_TOUCH_MAX_DELTA;
	else if (scaled > LINUX_TOUCH_MAX_DELTA)
		scaled = LINUX_TOUCH_MAX_DELTA;
	return (int)scaled;
}

static void input_reset_abs(InputBridge *in, int device)
{
	in->abs_x_valid[device] = 0;
	in->abs_y_valid[device] = 0;
	in->abs_pending_x_valid[device] = 0;
	in->abs_pending_y_valid[device] = 0;
	in->abs_rem_x[device] = 0;
	in->abs_rem_y[device] = 0;
}

static int input_finish_abs(InputBridge *in, int device)
{
	int active = !in->contact_seen[device] || in->contact[device];
	int dx = 0;
	int dy = 0;

	if (!active) {
		input_reset_abs(in, device);
		return 0;
	}
	if (in->abs_pending_x_valid[device])
		dx = input_abs_delta(in->abs_pending_x[device],
			&in->abs_x[device], &in->abs_x_valid[device],
			&in->abs_rem_x[device], in->sensitivity, 0);
	if (in->abs_pending_y_valid[device])
		dy = input_abs_delta(in->abs_pending_y[device],
			&in->abs_y[device], &in->abs_y_valid[device],
			&in->abs_rem_y[device], in->sensitivity, 1);
	in->abs_pending_x_valid[device] = 0;
	in->abs_pending_y_valid[device] = 0;
	if (!dx && !dy)
		return 0;
	in->dx += dx;
	in->dy += dy;
	if (in->dx < -LINUX_TOUCH_MAX_DELTA)
		in->dx = -LINUX_TOUCH_MAX_DELTA;
	else if (in->dx > LINUX_TOUCH_MAX_DELTA)
		in->dx = LINUX_TOUCH_MAX_DELTA;
	if (in->dy < -LINUX_TOUCH_MAX_DELTA)
		in->dy = -LINUX_TOUCH_MAX_DELTA;
	else if (in->dy > LINUX_TOUCH_MAX_DELTA)
		in->dy = LINUX_TOUCH_MAX_DELTA;
	in->mouse_dirty = 1;
	return 1;
}

static int linux_input_poll(InputBridge *in, PC *pc)
{
	int i;
	int activity = 0;

	if (!pc || !in)
		return 0;
	for (i = 0; i < in->count; i++) {
		struct input_event ev;

		while (read(in->fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
//Any actual evdev traffic MUST wake the short input burst. 
//CapTIvate often reports several sub-pixel ABS samples before one becomes a PS/2 delta
//Waiting for that delta made the pointer look completely frozen
			
			activity = 1;
			if (ev.type == EV_KEY) {
				if (ev.code >= BTN_MOUSE && ev.code <= BTN_TASK) {
					linux_input_button(in, ev.code, ev.value != 0);
					input_send_mouse(in, pc);
				} else if (ev.code == BTN_TOUCH ||
					   ev.code == BTN_TOOL_FINGER) {
					in->contact_seen[i] = 1;
					in->contact[i] = ev.value != 0;
				} else if (ev.value != 2) {
					linux_input_key(pc, ev.code, ev.value != 0);
				}
			} else if (ev.type == EV_REL) {
				if (ev.code == REL_X)
					in->dx += ev.value;
				else if (ev.code == REL_Y)
					in->dy += ev.value;
				else if (ev.code == REL_WHEEL)
					in->dz += ev.value;
				if (ev.value)
					in->mouse_dirty = 1;
			} else if (ev.type == EV_ABS) {
				if (ev.code == ABS_MT_SLOT) {
					in->mt_slot[i] = ev.value;
				} else if (ev.code == ABS_X ||
					   (ev.code == ABS_MT_POSITION_X &&
					    in->mt_slot[i] == 0)) {
					in->abs_pending_x[i] = ev.value;
					in->abs_pending_x_valid[i] = 1;
				} else if (ev.code == ABS_Y ||
					   (ev.code == ABS_MT_POSITION_Y &&
					    in->mt_slot[i] == 0)) {
					in->abs_pending_y[i] = ev.value;
					in->abs_pending_y_valid[i] = 1;
				}
			} else if (ev.type == EV_SYN) {
				if (ev.code == SYN_DROPPED) {
					in->dx = in->dy = in->dz = 0;
					input_reset_abs(in, i);
					in->mouse_dirty = 0;
					continue;
				}
				if (ev.code != SYN_REPORT)
					continue;
				input_finish_abs(in, i);
				input_send_mouse(in, pc);
			}
		}
	}
	return activity;
}

#endif

#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
static PC *crash_pc;
static unsigned long long crash_steps;

void nspire_log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fputs("[nspire-diag] ", stderr);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

void *pcmalloc(long size)
{
	return calloc(1, (size_t)size);
}
#endif

// platform HAL implementation
#include <time.h>
#if defined(BUILD_NSPIRE) || defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
void msleep(unsigned int milliseconds)
{
	struct timespec delay;
	delay.tv_sec = milliseconds / 1000U;
	delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	nanosleep(&delay, NULL);
}
#endif

#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
#define GUEST_HZ 4770000ULL
#define CYCLE_SCALE 1U
#define IDLE_INSNS 12288ULL
static uint32_t guest_ticks;
static uint32_t cycle_scale = CYCLE_SCALE;
static uint32_t last_cycle;
static uint64_t cycle_rem;
#ifdef TINY386_HEADLESS_DIAG
static uint32_t timer_base_us;
static int realtime_timer;
static int idle_assist_enabled;
static uint32_t idle_rounds;
static uint32_t idle_wait_us;

static uint32_t monotonic_uticks(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint32_t)ts.tv_sec * 1000000U +
		(uint32_t)ts.tv_nsec / 1000U);
}

static uint64_t monotonic_millis(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL +
		(uint64_t)ts.tv_nsec / 1000000ULL;
}
#endif

static void dump_guest_state(const char *tag, PC *pc,
				       unsigned long long steps)
{
	CPUI386Snapshot snap;

	if (!pc || !pc->cpu) {
		fprintf(stderr, "%s: no cpu state available\n", tag);
		return;
	}
	cpui386_snapshot(pc->cpu, &snap);
	fprintf(stderr,
		"%s: loop=%llu cycles=%ld cs:ip=%04x:%08x phys=%08x "
		"cr0=%08x cr2=%08x cr3=%08x fl=%08x h%d if%d cpl%d vm%d "
		"op=%02x %02x %02x %02x %02x %02x %02x %02x "
		"ax=%08x bx=%08x cx=%08x dx=%08x si=%08x di=%08x\n",
		tag, steps, snap.cycles, snap.cs, snap.ip, snap.phys,
		snap.cr0, snap.cr2, snap.cr3, snap.flags,
		snap.halt, snap.interrupt_enabled, snap.cpl, snap.vm86,
		snap.code[0], snap.code[1], snap.code[2], snap.code[3],
		snap.code[4], snap.code[5], snap.code[6], snap.code[7],
		snap.gpr[0], snap.gpr[3], snap.gpr[1], snap.gpr[2],
		snap.gpr[6], snap.gpr[7]);
}


#ifndef _WIN32
static void handle_fatal_signal(int signum, siginfo_t *info,
					  void *context)
{
	ucontext_t *uc = (ucontext_t *)context;

	fprintf(stderr, "FATAL: signal %d during tiny386 loop\n", signum);
	if (info)
		fprintf(stderr, "fatal-host: si_code=%d fault_addr=%p\n",
			info->si_code, info->si_addr);
#if defined(__arm__)
	if (uc) {
		fprintf(stderr,
			"fatal-host-arm: pc=%08lx lr=%08lx sp=%08lx cpsr=%08lx\n",
			(unsigned long)uc->uc_mcontext.arm_pc,
			(unsigned long)uc->uc_mcontext.arm_lr,
			(unsigned long)uc->uc_mcontext.arm_sp,
			(unsigned long)uc->uc_mcontext.arm_cpsr);
	}
#elif defined(__aarch64__)
	if (uc) {
		fprintf(stderr,
			"fatal-host-a64: pc=%016llx lr=%016llx sp=%016llx\n",
			(unsigned long long)uc->uc_mcontext.pc,
			(unsigned long long)uc->uc_mcontext.regs[30],
			(unsigned long long)uc->uc_mcontext.sp);
	}
#endif
	dump_guest_state("fatal-state", crash_pc, crash_steps);
	fflush(stderr);
	_Exit(128 + signum);
}
#endif

static void install_signals(void)
{
#ifndef _WIN32
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handle_fatal_signal;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
#endif
}

static uint32_t parse_env_u32(const char *name, uint32_t fallback,
			      uint32_t min_value, uint32_t max_value)
{
	const char *text = getenv(name);
	unsigned long value;
	char *end = NULL;

	if (!text || !*text)
		return fallback;
	value = strtoul(text, &end, 0);
	if (!end || *end || value < min_value || value > max_value)
		return fallback;
	return (uint32_t)value;
}


uint32_t get_uticks()
{
#ifdef TINY386_HEADLESS_DIAG
	if (realtime_timer)
		return monotonic_uticks() - timer_base_us;
#endif
	return guest_ticks;
}

static void reset_guest_timer(void)
{
	guest_ticks = 0;
	last_cycle = 0;
	cycle_rem = 0;
}

static void sync_guest_timer(PC *pc)
{
#ifdef TINY386_HEADLESS_DIAG
	if (realtime_timer) {
		guest_ticks = monotonic_uticks() - timer_base_us;
		return;
	}
#endif
	if (pc && pc->cpu) {
		uint32_t current_cycle = (uint32_t)cpui386_get_cycle(pc->cpu);
		uint32_t delta = current_cycle - last_cycle;
		uint64_t scaled_cycles;
		uint64_t scaled_hz;

		if (!delta)
			delta = IDLE_INSNS;
		last_cycle = current_cycle;
		scaled_cycles = (uint64_t)delta *
				cycle_scale *
				1000000ULL + cycle_rem;
		scaled_hz = GUEST_HZ;
		guest_ticks += (uint32_t)(scaled_cycles / scaled_hz);
		cycle_rem = scaled_cycles % scaled_hz;
	}
}

#ifdef TINY386_HEADLESS_DIAG
static int guest_waits_irq(PC *pc)
{
	CPUI386Snapshot snap;

	if (!pc || !pc->cpu)
		return 0;
	cpui386_snapshot(pc->cpu, &snap);
	return snap.halt && snap.interrupt_enabled;
}

static void realtime_idle_wait(PC *pc)
{//Win2k uses the host clock for PIT & RTC time. Yield for 1ms so pc_step can deliver pending PIT interrupt and not starve RNDIS

	if (realtime_timer && guest_waits_irq(pc))
		msleep(1);
}

static void assist_idle(PC *pc)
{
	unsigned round;

	if (!idle_assist_enabled || realtime_timer ||
	    !guest_waits_irq(pc))
		return;
	for (round = 0; round < idle_rounds; round++) {
		guest_ticks += idle_wait_us;
		pc_step(pc);
		if (!guest_waits_irq(pc))
			break;
	}
}
#endif
#else
uint32_t get_uticks()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint32_t) ts.tv_sec * 1000000 +
		(uint32_t) ts.tv_nsec / 1000);
}
#endif

#if defined(BUILD_NSPIRE)
void *bigmalloc(size_t size)
{
	return calloc(1, size);
}
#elif !defined(_WIN32)
#include <sys/mman.h>
void *bigmalloc(size_t size)
{
	return mmap(NULL, size, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
#else
void *bigmalloc(size_t size)
{
	return malloc(size);
}
#endif

#ifdef TINY386_FB_MIRROR
typedef struct FBMirror {
	int fd;
	uint8_t *mem;
	size_t mem_size;
	int width;
	int height;
	int xoffset;
	int yoffset;
	int xres_virtual;
	int yres_virtual;
	int bpp;
	int line_length;
	int guest_width;
	int guest_height;
	int rotate;
	unsigned long presents;
	uint16_t *shadow;
	size_t shadow_pixels;
	int shadow_valid;
	uint8_t *dirty_rows;
	int dirty_count;
} FBMirror;

static void fbmirror_close(FBMirror *m)
{
	if (!m || m->fd < 0)
		return;
	if (m->mem && m->mem != MAP_FAILED)
		munmap(m->mem, m->mem_size);
	free(m->shadow);
	free(m->dirty_rows);
	close(m->fd);
	m->fd = -1;
	m->mem = NULL;
	m->shadow = NULL;
	m->shadow_pixels = 0;
	m->shadow_valid = 0;
	m->dirty_rows = NULL;
	m->dirty_count = 0;
}

static int fbmirror_open(FBMirror *m, int guest_width, int guest_height)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	const char *path = getenv("TINY386_FBDEV");
	const char *rotate = getenv("TINY386_FB_MIRROR_ROTATE");

	memset(m, 0, sizeof(*m));
	m->fd = -1;
	m->guest_width = guest_width;
	m->guest_height = guest_height;
	if (!path || !*path)
		path = "/dev/fb0";
	m->fd = open(path, O_RDWR);
	if (m->fd < 0) {
		fprintf(stderr, "fbmirror: open %s failed: %s\n",
			path, strerror(errno));
		return -1;
	}
	if (ioctl(m->fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    ioctl(m->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		fprintf(stderr, "fbmirror: ioctl failed: %s\n", strerror(errno));
		fbmirror_close(m);
		return -1;
	}
	m->width = (int)var.xres;
	m->height = (int)var.yres;
	m->xoffset = (int)var.xoffset;
	m->yoffset = (int)var.yoffset;
	m->xres_virtual = (int)var.xres_virtual;
	m->yres_virtual = (int)var.yres_virtual;
	m->bpp = (int)var.bits_per_pixel;
	m->line_length = (int)fix.line_length;
	m->mem_size = (size_t)fix.smem_len;
	if (rotate && (!strcmp(rotate, "cw") || !strcmp(rotate, "right"))) {
		m->rotate = 1;
	} else if (rotate && (!strcmp(rotate, "ccw") || !strcmp(rotate, "left"))) {
		m->rotate = 2;
	} else if (rotate && (!strcmp(rotate, "0") || !strcmp(rotate, "none"))) {
		m->rotate = 0;
	} else if (m->width == guest_height && m->height == guest_width) {
		m->rotate = 1;
	}
	m->mem = mmap(NULL, m->mem_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED, m->fd, 0);
	if (m->mem == MAP_FAILED) {
		fprintf(stderr, "fbmirror: mmap failed: %s\n", strerror(errno));
		fbmirror_close(m);
		return -1;
	}
	m->shadow_pixels = (size_t)guest_width * (size_t)guest_height;
	m->shadow = malloc(m->shadow_pixels * sizeof(m->shadow[0]));
	if (m->shadow)
		memset(m->shadow, 0, m->shadow_pixels * sizeof(m->shadow[0]));
	m->dirty_rows = malloc((size_t)guest_height);
	if (m->dirty_rows) {
		memset(m->dirty_rows, 1, (size_t)guest_height);
		m->dirty_count = guest_height;
	}
	memset(m->mem, 0, m->mem_size);
	fprintf(stderr, "fbmirror: %s %dx%d virt=%dx%d off=%d,%d bpp=%d line=%d guest=%dx%d rotate=%d dirty=%d\n",
		path, m->width, m->height, m->xres_virtual, m->yres_virtual,
		m->xoffset, m->yoffset, m->bpp, m->line_length,
		guest_width, guest_height, m->rotate,
		m->shadow != NULL && m->dirty_rows != NULL);
	return 0;
}

static void fbmirror_mark_dirty(FBMirror *m, int y, int h)
{
	int first;
	int last;
	int row;

	if (!m || !m->dirty_rows || h <= 0)
		return;
	first = y < 0 ? 0 : y;
	last = y + h;
	if (last > m->guest_height)
		last = m->guest_height;
	for (row = first; row < last; row++) {
		if (!m->dirty_rows[row]) {
			m->dirty_rows[row] = 1;
			m->dirty_count++;
		}
	}
}

static void fbmirror_clear_dirty(FBMirror *m)
{
	if (!m || !m->dirty_rows)
		return;
	memset(m->dirty_rows, 0, (size_t)m->guest_height);
	m->dirty_count = 0;
}
//since the guest framebuffer and LCD mirror have separate dirty states
//a full guest update is not enough to repair a lost host row after the mirror recorded its shadow


static void fbmirror_request_full(FBMirror *m)
{
	if (!m)
		return;
	m->shadow_valid = 0;
	if (m->dirty_rows) {
		memset(m->dirty_rows, 1, (size_t)m->guest_height);
		m->dirty_count = m->guest_height;
	}
}

static int fbmirror_row_dirty(FBMirror *m, const uint16_t *src, int y)
{
	const uint16_t *row;
	uint16_t *shadow_row;
	size_t bytes;

	if (!m->shadow)
		return 1;
	row = src + (size_t)y * m->guest_width;
	shadow_row = m->shadow + (size_t)y * m->guest_width;
	bytes = (size_t)m->guest_width * sizeof(row[0]);
	if (m->shadow_valid && memcmp(row, shadow_row, bytes) == 0)
		return 0;
	memcpy(shadow_row, row, bytes);
	return 1;
}

static void fbmirror_store(FBMirror *m, int dx, int dy, uint16_t p)
{
	uint8_t *dst;

	if (!m || !m->mem || m->mem == MAP_FAILED ||
	    dx < 0 || dy < 0 || dx >= m->width || dy >= m->height)
		return;
	dst = m->mem + (size_t)dy * m->line_length;
	if (m->bpp == 16) {
		((uint16_t *)dst)[dx] = p;
	} else if (m->bpp == 32) {
		uint32_t r = (p >> 11) & 0x1f;
		uint32_t g = (p >> 5) & 0x3f;
		uint32_t b = p & 0x1f;
		((uint32_t *)dst)[dx] = (r << 19) | (g << 10) | (b << 3);
	}
}

static void fbmirror_present(FBMirror *m, const void *guest_fb)
{
	const uint16_t *src = (const uint16_t *)guest_fb;
	int y;

	if (!m || !m->mem || m->mem == MAP_FAILED || !src)
		return;
	if (m->dirty_rows && !m->dirty_count)
		return;
	m->presents++;
	if (m->width == m->guest_width && m->height == m->guest_height &&
	    !m->rotate && m->bpp == 16) {
		for (y = 0; y < m->guest_height; y++) {
			const uint16_t *row = src + (size_t)y * m->guest_width;
			uint8_t *dst = m->mem + (size_t)y * m->line_length;
			if (m->dirty_rows && !m->dirty_rows[y])
				continue;
			if (fbmirror_row_dirty(m, src, y))
				memcpy(dst, row, (size_t)m->guest_width * 2);
		}
		m->shadow_valid = 1;
		fbmirror_clear_dirty(m);
		return;
	}
	if (m->bpp == 16 && m->width == m->guest_height &&
	    m->height == m->guest_width && (m->rotate == 1 || m->rotate == 2)) {
		int sx;

		for (y = 0; y < m->guest_height; y++) {
			const uint16_t *row = src + (size_t)y * m->guest_width;
			int dx;

			if ((m->dirty_rows && !m->dirty_rows[y]) ||
			    !fbmirror_row_dirty(m, src, y))
				continue;
			dx = (m->rotate == 1) ? (m->guest_height - 1 - y) : y;
			for (sx = 0; sx < m->guest_width; sx++) {
				int dy = (m->rotate == 1) ? sx : (m->guest_width - 1 - sx);
				uint16_t *dst = (uint16_t *)(m->mem + (size_t)dy * m->line_length);
				dst[dx] = row[sx];
			}
		}
		m->shadow_valid = 1;
		fbmirror_clear_dirty(m);
		return;
	}
	for (y = 0; y < m->height; y++) {
		int x;
		if (!m->rotate && m->dirty_rows) {
			int sy = y * m->guest_height / m->height;
			if (!m->dirty_rows[sy])
				continue;
		}
		for (x = 0; x < m->width; x++) {
			int sx, sy;
			if (m->rotate == 2) {
				sx = m->guest_width - 1 - y * m->guest_width / m->height;
				sy = x * m->guest_height / m->width;
			} else if (m->rotate == 1) {
				sx = y * m->guest_width / m->height;
				sy = m->guest_height - 1 - x * m->guest_height / m->width;
			} else {
				sx = x * m->guest_width / m->width;
				sy = y * m->guest_height / m->height;
			}
			fbmirror_store(m, x, y,
				       src[(size_t)sy * m->guest_width + sx]);
		}
	}
	fbmirror_clear_dirty(m);
}

#endif

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
	FILE *fp = fopen(file, "rb");
	size_t read_len;
	if (fp == NULL) {
		fprintf(stderr, "load_rom: open %s failed: %s\n", file, strerror(errno));
		abort();
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fprintf(stderr, "load_rom: seek %s failed\n", file);
		fclose(fp);
		abort();
	}
	int len = ftell(fp);
	if (len < 0) {
		fprintf(stderr, "load_rom: length %s failed\n", file);
		fclose(fp);
		abort();
	}
	fprintf(stderr, "load_rom: %s, len %d\n", file, len);
	rewind(fp);
	if (backward)
		read_len = fread(phys_mem + addr - len, 1, len, fp);
	else
		read_len = fread(phys_mem + addr, 1, len, fp);
	fclose(fp);
	if (read_len != (size_t)len) {
		fprintf(stderr, "load_rom: short read %s\n", file);
		abort();
	}
	return len;
}

#ifdef TINY386_LINUX_FB
typedef struct LinuxFB {
	int fd;
	uint8_t *mem;
	size_t mem_size;
	int width;
	int height;
	int bpp;
	int line_length;
	uint8_t *guest_fb;
	int guest_width;
	int guest_height;
	unsigned long presents;
} LinuxFB;

static void linuxfb_close(LinuxFB *fb)
{
	if (!fb || fb->fd < 0)
		return;
	if (fb->mem && fb->mem != MAP_FAILED)
		munmap(fb->mem, fb->mem_size);
	close(fb->fd);
	fb->fd = -1;
	fb->mem = NULL;
}

static int linuxfb_open(LinuxFB *fb, uint8_t *guest_fb, int guest_width,
			int guest_height)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	const char *path = getenv("TINY386_FBDEV");

	memset(fb, 0, sizeof(*fb));
	fb->fd = -1;
	fb->guest_fb = guest_fb;
	fb->guest_width = guest_width;
	fb->guest_height = guest_height;
	if (!path || !*path)
		path = "/dev/fb0";
	fb->fd = open(path, O_RDWR);
	if (fb->fd < 0) {
		fprintf(stderr, "linuxfb: open %s failed: %s\n", path,
			strerror(errno));
		return -1;
	}
	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		fprintf(stderr, "linuxfb: ioctl failed: %s\n", strerror(errno));
		linuxfb_close(fb);
		return -1;
	}
	fb->width = (int)var.xres;
	fb->height = (int)var.yres;
	fb->bpp = (int)var.bits_per_pixel;
	fb->line_length = (int)fix.line_length;
	fb->mem_size = (size_t)fix.smem_len;
	fb->mem = mmap(NULL, fb->mem_size, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fb->fd, 0);
	if (fb->mem == MAP_FAILED) {
		fprintf(stderr, "linuxfb: mmap failed: %s\n", strerror(errno));
		linuxfb_close(fb);
		return -1;
	}
	memset(fb->mem, 0, fb->mem_size);
	msync(fb->mem, fb->mem_size, MS_SYNC);
	fprintf(stderr, "linuxfb: %s %dx%d bpp=%d line=%d guest=%dx%d\n",
		path, fb->width, fb->height, fb->bpp, fb->line_length,
		guest_width, guest_height);
	return 0;
}

static void linuxfb_present(LinuxFB *fb, int x, int y, int w, int h)
{
	int yy;

	if (!fb || !fb->mem || fb->mem == MAP_FAILED || !fb->guest_fb)
		return;
	fb->presents++;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x >= fb->guest_width || y >= fb->guest_height || w <= 0 || h <= 0)
		return;
	if (x + w > fb->guest_width)
		w = fb->guest_width - x;
	if (y + h > fb->guest_height)
		h = fb->guest_height - y;

	if (fb->width == fb->guest_width && fb->height == fb->guest_height) {
		for (yy = y; yy < y + h; yy++) {
#if BPP == 16
			uint8_t *dst = fb->mem + (size_t)yy * fb->line_length;
			const uint16_t *src = (const uint16_t *)fb->guest_fb +
				(size_t)yy * fb->guest_width + x;
			if (fb->bpp == 16) {
				memcpy(dst + x * 2, src, (size_t)w * 2);
			} else if (fb->bpp == 32) {
				int xx;
				uint32_t *d32 = (uint32_t *)(dst + x * 4);
				for (xx = 0; xx < w; xx++) {
					uint16_t p = src[xx];
					uint32_t r = (p >> 11) & 0x1f;
					uint32_t g = (p >> 5) & 0x3f;
					uint32_t b = p & 0x1f;
					d32[xx] = (r << 19) | (g << 10) | (b << 3);
				}
			}
#else
			uint8_t *dst = fb->mem + (size_t)yy * fb->line_length;
			const uint8_t *src = fb->guest_fb +
				((size_t)yy * fb->guest_width + x) * 4;
			if (fb->bpp == 32) {
				memcpy(dst + x * 4, src, (size_t)w * 4);
			} else if (fb->bpp == 16) {
				int xx;
				uint16_t *d16 = (uint16_t *)(dst + x * 2);
				for (xx = 0; xx < w; xx++) {
					const uint8_t *p = src + xx * 4;
					uint16_t r = p[2] >> 3;
					uint16_t g = p[1] >> 2;
					uint16_t b = p[0] >> 3;
					d16[xx] = (uint16_t)((r << 11) |
							      (g << 5) | b);
				}
			}
#endif
		}
		return;
	}

	for (yy = 0; yy < fb->height; yy++) {
		int xx;
		int sy = yy * fb->guest_height / fb->height;
		uint8_t *dst = fb->mem + (size_t)yy * fb->line_length;
		for (xx = 0; xx < fb->width; xx++) {
			int sx = xx * fb->guest_width / fb->width;
#if BPP == 16
			uint16_t p = ((uint16_t *)fb->guest_fb)
				[(size_t)sy * fb->guest_width + sx];
			if (fb->bpp == 16) {
				((uint16_t *)dst)[xx] = p;
			} else if (fb->bpp == 32) {
				uint32_t r = (p >> 11) & 0x1f;
				uint32_t g = (p >> 5) & 0x3f;
				uint32_t b = p & 0x1f;
				((uint32_t *)dst)[xx] =
					(r << 19) | (g << 10) | (b << 3);
			}
#else
			if (fb->bpp == 32) {
				((uint32_t *)dst)[xx] =
					((uint32_t *)fb->guest_fb)
					[(size_t)sy * fb->guest_width + sx];
			} else if (fb->bpp == 16) {
				const uint8_t *p = fb->guest_fb +
					((size_t)sy * fb->guest_width + sx) * 4;
				uint16_t r = p[2] >> 3;
				uint16_t g = p[1] >> 2;
				uint16_t b = p[0] >> 3;
				((uint16_t *)dst)[xx] =
					(uint16_t)((r << 11) | (g << 5) | b);
			}
#endif
		}
	}
}

#endif

static void redraw(void *opaque,
		   int x, int y, int w, int h)
{
#ifdef TINY386_LINUX_FB
	linuxfb_present((LinuxFB *)opaque, x, y, w, h);
#elif defined(TINY386_FB_MIRROR)
	(void)x;
	(void)w;
	fbmirror_mark_dirty((FBMirror *)opaque, y, h);
#else
	(void)opaque;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
#endif
}

int main(int argc, char *argv[])
{
	PCConfig conf;
	const char *net_ping_every_text;
	const char *vga_every_env;
	const char *vga_full_env;
	const char *vga_watchdog_env;
	const char *vga_input_env;
	const char *vga_burst_env;
	unsigned long long net_ping_every = 0;
	unsigned long long vga_every = 0;
	unsigned long long vga_full_every = 0;
	unsigned long long vga_watchdog_ms = 60000;
	unsigned long long vga_input_every = 8;
	unsigned long long vga_input_burst = 128;
	unsigned long long early_vga_steps = 512;
	setvbuf(stderr, NULL, _IONBF, 0);
#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
	install_signals();
#endif
	memset(&conf, 0, sizeof(conf));
	conf.mem_size = 8 * 1024 * 1024;
	conf.vga_mem_size = 256 * 1024;
	conf.width = 720;
	conf.height = 480;
	conf.cpu_gen = 4;
	conf.fpu = 0;

	const char *argv1;
	bool enable_kvm = false;
#ifdef CALC_PROFILE_SELECT
	char selected_config[CALC_CONFIG_PATH_MAX];

	if (argc == 1) {
		if (config_select(selected_config, sizeof(selected_config)) < 0)
			return 1;
		argv1 = selected_config;
	} else
#endif
	if (argc == 2) {
		argv1 = argv[1];
	} else if (argc == 3) {
		if (strcmp(argv[1], "-kvm") == 0)
			enable_kvm = true;
		argv1 = argv[2];
	} else {
		return 1;
	}

#ifdef TINY386_HEADLESS_DIAG
	profile_apply(argv1);
#endif

	int err = ini_parse(argv1, parse_conf_ini, &conf);
	if (err) {
		fprintf(stderr, "error %d\n", err);
		return err;
	}
	if (enable_kvm)
		conf.cpu_gen = -1;

	net_ping_every_text = getenv("TINY386_NET_PING_EVERY");
	if (net_ping_every_text)
		net_ping_every = strtoull(net_ping_every_text, NULL, 0);
	vga_every_env = getenv(E_VGA);
	if (vga_every_env)
		vga_every = strtoull(vga_every_env, NULL, 0);
	vga_full_env = getenv(E_VGA_FULL);
	if (vga_full_env)
		vga_full_every =
			strtoull(vga_full_env, NULL, 0);
	vga_watchdog_env =
		getenv("TINY386_HEADLESS_VGA_WATCHDOG_MS");
	if (vga_watchdog_env)
		vga_watchdog_ms =
			strtoull(vga_watchdog_env, NULL, 0);
	vga_input_env =
		getenv(E_INPUT);
	if (vga_input_env)
		vga_input_every =
			strtoull(vga_input_env, NULL, 0);
	vga_burst_env =
		getenv(E_BURST);
	if (vga_burst_env)
		vga_input_burst =
			strtoull(vga_burst_env, NULL, 0);

	unsigned long long steps = 0;
	unsigned long long input_vga_until = 0;
	uint64_t next_full_vga_ms = vga_watchdog_ms ?
		monotonic_millis() + vga_watchdog_ms : 0;
	void *fb = bigmalloc(conf.width * conf.height * (BPP / 8));
	void *redraw_opaque = NULL;
#ifdef TINY386_LINUX_FB
		LinuxFB linuxfb;
#endif
#ifdef TINY386_FB_MIRROR
		FBMirror fbmirror;
		int fbmirror_enabled = 0;
#endif
#if defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
		ConsoleOwner linux_console;
		InputBridge linux_input;
		int linux_console_enabled = 0;
		int linux_input_enabled = 0;
#endif
		PC *pc;

	fprintf(stderr, "winspire: starting\n");
#ifdef TINY386_LINUX_FB
		if (linuxfb_open(&linuxfb, fb, conf.width, conf.height) == 0)
			redraw_opaque = &linuxfb;
#endif
#ifdef TINY386_FB_MIRROR
		if (fbmirror_open(&fbmirror, conf.width, conf.height) == 0) {
			fbmirror_enabled = 1;
#ifndef TINY386_LINUX_FB
			redraw_opaque = &fbmirror;
#endif
		}
#endif
#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
		reset_guest_timer();
#ifdef TINY386_HEADLESS_DIAG
		timer_base_us = monotonic_uticks();
		realtime_timer =
			parse_env_u32(E_RT, 0, 0, 1);
		idle_assist_enabled =
			parse_env_u32(E_IDLE, 1, 0, 1);
		idle_rounds =
			parse_env_u32("TINY386_IDLE_ASSIST_ROUNDS", 16, 1, 256);
		idle_wait_us =
			parse_env_u32("TINY386_IDLE_ASSIST_USEC", 10000U,
				      1000U, 50000U);
#endif
		cycle_scale = parse_env_u32(E_SCALE, CYCLE_SCALE, 1U, 64U);
#endif
		pc = pc_new(redraw, redraw_opaque, fb, &conf);
		if (!pc) {
			fprintf(stderr, "FATAL: pc_new failed before CPU state was available\n");
			return 20;
		}
#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
		crash_pc = pc;
		crash_steps = 0;
#endif
#if defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
		linux_console_init(&linux_console);
		linux_console_enabled = 1;
		linux_input_init(&linux_input);
		linux_input_enabled = 1;
#endif
		load_bios_and_reset(pc);
#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
#ifdef TINY386_LINUX_FB
		vga_refresh(pc->vga, redraw, redraw_opaque, 1);
#elif defined(TINY386_FB_MIRROR)
		vga_refresh(pc->vga, redraw, redraw_opaque, 1);
		if (fbmirror_enabled)
			fbmirror_present(&fbmirror, fb);
#endif
#endif
		i8254_rebase(pc->pit);
		pc->boot_start_time = get_uticks();
		for (; pc->shutdown_state != 8; steps++) {
			int did_vga_step = 0;
			int input_activity = 0;
#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
			crash_steps = steps;
#endif
			pc_step(pc);
#if defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
			if (linux_input_enabled)
				input_activity = linux_input_poll(&linux_input, pc);
			if (input_activity && vga_input_burst) {
				if (~steps < vga_input_burst)
					input_vga_until = ~0ULL;
				else
					input_vga_until = steps + vga_input_burst;
			}
#endif
#if defined(BUILD_NSPIRE) || defined(TINY386_HEADLESS_DIAG)
			sync_guest_timer(pc);
#ifdef TINY386_HEADLESS_DIAG
			realtime_idle_wait(pc);
			assist_idle(pc);
#endif
#ifdef TINY386_LINUX_FB
			if (redraw_opaque && early_vga_steps &&
			    steps < early_vga_steps) {
				pc_vga_step(pc);
				did_vga_step = 1;
			}
#endif
			{
				unsigned long long vga_every_now = vga_every;
				int request_full_vga = 0;
				uint64_t now_ms = 0;

				if (vga_input_every && steps <= input_vga_until)
					vga_every_now = vga_input_every;
				if (next_full_vga_ms && (steps & 31ULL) == 0) {
					now_ms = monotonic_millis();
					if (now_ms >= next_full_vga_ms) {
						pc->full_update = 2;
						if (fbmirror_enabled)
							fbmirror_request_full(&fbmirror);
						pc_vga_step(pc);
						did_vga_step = 1;
						next_full_vga_ms = now_ms +
							vga_watchdog_ms;
					}
				}
				if (!did_vga_step && vga_every_now && steps &&
				    (steps % vga_every_now) == 0) {
					//Wishful thinking! hoping to catch a late taskbar draw!
					request_full_vga = vga_full_every &&
						(steps % vga_full_every) == 0;
					if (request_full_vga)
						pc->full_update = 2;
					if (request_full_vga && fbmirror_enabled)
						fbmirror_request_full(&fbmirror);
					pc_vga_step(pc);
					did_vga_step = 1;
				}
			}
#ifdef TINY386_FB_MIRROR
			if (did_vga_step && fbmirror_enabled)
				fbmirror_present(&fbmirror, fb);
#endif
			if (net_ping_every && steps && (steps % net_ping_every) == 0) {
				CPUI386Snapshot snap;
				uint8_t code[8];
				int i;

				memset(&snap, 0, sizeof(snap));
				if (pc && pc->cpu)
					cpui386_snapshot(pc->cpu, &snap);
				for (i = 0; i < 8; i++)
					code[i] = (uint8_t)snap.code[i];
				ide_net_ping(pc->ide, steps, (uint32_t)snap.cycles,
					     snap.cs, snap.ip, snap.phys,
					     snap.flags,
					     (uint32_t)(snap.halt |
							(snap.interrupt_enabled << 1) |
							(snap.cpl << 2) |
							(snap.vm86 << 4)),
					     code);
			}
#else
			pc_vga_step(pc);
#endif
		}
#if defined(TINY386_LINUX_FB) || defined(TINY386_FB_MIRROR)
		if (linux_input_enabled)
			linux_input_close(&linux_input);
		if (linux_console_enabled)
			linux_console_close(&linux_console);
#endif
#ifdef BUILD_NSPIRE
		pc_free_buffers(pc);
		free(fb);
#endif
#ifdef TINY386_LINUX_FB
		linuxfb_close(&linuxfb);
#endif
#ifdef TINY386_FB_MIRROR
		if (fbmirror_enabled)
			fbmirror_close(&fbmirror);
#endif
	return 0;
}
