#ifndef WINSPIRE_RUNTIME_POLICY_H
#define WINSPIRE_RUNTIME_POLICY_H
/* Shared Server and desktop runtime defaults; explicit overrides win. */
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
#define E_TIMER_READS "TINY386_TIMER_READS_TRACK_CPU"

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
	const char *windows_base = strrchr(config_path, '\\');
	if (windows_base && (!base || windows_base > base)) base = windows_base;
	return base ? base + 1 : config_path;
}

static int profile_is_win9x(const char *config_path)
{
	const char *p = profile_name(config_path);
	return !strncmp(p, "win9x", 5) || !strncmp(p, "win95", 5) ||
	       !strncmp(p, "win98", 5) || !strncmp(p, "winme", 5);
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
	} else if (profile_is_win9x(config_path)) {
		set_default(E_RT, "0");
		set_default(E_SCALE, "12");
		set_default(E_IDLE, "0");
		set_default(E_MOVS, "0");
		set_default(E_STOS, "0");
		/* Service retrace every batch, matching the nspire95 frontend. */
		set_default(E_VGA, "1");
		set_default(E_VGA_FULL, "0");
		/* Refresh immediately after input. */
		set_default(E_INPUT, "1");
		set_default(E_BURST, "256");
		/* Briefly hold transitions, then reveal real DOS prompts. */
		set_default(E_TEXT, "4");
		set_default(E_2K_FAST, "0");
	}
	set_default(E_TIMER_READS, profile_is_win9x(config_path) ? "1" : "0");
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
