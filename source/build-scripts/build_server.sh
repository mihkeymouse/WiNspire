#!/usr/bin/env bash
set -euo pipefail

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$script_dir/../.." && pwd)
src=$repo/source/winspire
work_root=$repo/build/server-work
emulator_out=$work_root/emulator
binary=$emulator_out/winspire
donor=$repo/calc/linux/nspire-linux-micro-initrd.tns
out_root=$repo/build/Server/Calculator
helpers=$work_root/helpers
clock_restore=$helpers/clock_restore
time_sync=$helpers/sync_server_time
busybox_src=$repo/source/third-party/busybox-1.33.1.tar.bz2
busybox_config=$repo/source/third-party/busybox-1.33.1.config
busybox_out=$work_root/busybox
cc=${ARM_CC:-arm-linux-gnueabi-gcc}

rm -rf "$work_root" "$out_root"
mkdir -p "$emulator_out" "$helpers"

# Build the initrd shell from its shipped source and configuration.
mkdir -p "$busybox_out"
tar -xjf "$busybox_src" -C "$busybox_out" --strip-components=1
cp "$busybox_config" "$busybox_out/.config"
make -C "$busybox_out" ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- oldconfig </dev/null >/dev/null
make -C "$busybox_out" -j2 ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- >/dev/null

# Use static ARM binaries because the initrd has no C runtime.
cflags=(
	-I "$src" -Wall -Wno-unused-function -Wno-unused-variable
	-Wno-unused-result -marm -mcpu=arm926ej-s -mtune=arm926ej-s -O3
	"-ffile-prefix-map=$repo/=" "-fdebug-prefix-map=$repo/="
	"-fmacro-prefix-map=$repo/=" -fomit-frame-pointer -fno-stack-protector
	-fno-unwind-tables -fno-asynchronous-unwind-tables
	-ffunction-sections -fdata-sections -DWINSPIRE_SERVER_BUILD
	-include "$src/release_config.h"
)
core=(
	ini.c i386.c fpu.c i8259.c i8254.c ide.c vga.c i8042.c misc.c
	adlib.c ne2000.c i8257.c sb16.c pcspk.c fmopl.c pc.c pci.c
	win32.c osd/microui.c osd/osd.c main.c
)
objects=()
for source_file in "${core[@]}"; do
	object="$emulator_out/${source_file//\//_}.o"
	"$cc" "${cflags[@]}" -c "$src/$source_file" -o "$object"
	objects+=("$object")
done
"$cc" -static -Wl,--gc-sections -o "$binary" "${objects[@]}" -lm
arm-linux-gnueabi-strip "$binary"

helper_flags=(
	-Os -s -march=armv5te -marm -nostdlib -static -fno-builtin
	-fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
	-Wall -Wextra -Werror -Wl,-e,_start
)
"$cc" "${helper_flags[@]}" "$repo/source/nspire/clock_restore_arm.c" \
	-o "$clock_restore"
"$cc" "${helper_flags[@]}" "$repo/source/nspire/sync_server_time_arm.c" \
	-o "$time_sync"

for path in "$binary" "$donor" "$clock_restore" "$time_sync" \
	"$busybox_out/busybox"; do
	[ -f "$path" ] || { echo "missing required file: $path" >&2; exit 1; }
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/root" "$out_root/linux"
gzip -dc "$donor" | (cd "$work/root" && cpio -idmu --quiet)

rm -f "$work/root/bin/tiny386"
install -m 0755 "$binary" "$work/root/bin/winspire"
install -m 0755 "$busybox_out/busybox" "$work/root/bin/busybox"
install -m 0755 "$clock_restore" "$work/root/bin/clock_restore"
install -m 0755 "$time_sync" "$work/root/bin/sync_server_time"
rm -rf "$work/root/lib" "$work/root/usr/lib"
if [ -f "$work/root/tiny386.ini" ]; then
	mv "$work/root/tiny386.ini" "$work/root/winspire.ini"
fi
rm -f "$work/root/bin/winspire-input-setup"

sed -i \
	-e 's/banner "Launching tiny386"/banner "Launching WiNspire"/g' \
	-e 's#/bin/tiny386#/bin/winspire#g' \
	-e 's#/tiny386\.ini#/winspire.ini#g' \
	-e 's#/tmp/tiny386\.log#/tmp/winspire.log#g' \
	"$work/root/netinit.sh"
sed -i -e 's/\[Winspire\]/[WiNspire]/g' \
	-e 's/\[WinSpire\]/[WiNspire]/g' "$work/root/netinit.sh"
sed -i -e '/^banner "PACKAGE /d' \
	-e '/^if \[ -x \/bin\/clock_probe \]; then$/,/^fi$/d' "$work/root/netinit.sh"
rm -f "$work/root/bin/clock_probe"
# Remove donor exports that the current core no longer needs.
for obsolete in \
	TINY386_NET_RECONNECT_ATTEMPTS TINY386_NET_RECONNECT_DELAY_MS \
	TINY386_NET_CONNECT_TIMEOUT_MS TINY386_HEADLESS_VGA_ACTIVE_EVERY \
	TINY386_HEADLESS_VGA_BOOT_READS TINY386_LINUXFB_PROFILE \
	TINY386_ARM_DYNAREC TINY386_DYNAREC_STATS TINY386_DYNAREC_CACHE_KB \
	TINY386_DYNAREC_HOT_COUNT TINY386_NET_WRITEBACK_FLUSH_EVERY \
	TINY386_JIT_ALLOW_MEMORY TINY386_JIT_ALLOW_WRITES \
	TINY386_JIT_ALLOW_TERMINAL TINY386_JIT_ALLOW_JCC \
	TINY386_DTLB_FAST TINY386_ARM_FAST_OPS TINY386_BOOT_DIAG \
	TINY386_FB_MIRROR_SWAP16 TINY386_FB_MIRROR_LEGACY_RAW16; do
	sed -i "/^export ${obsolete}=/d" "$work/root/netinit.sh"
done
sed -i '/^if \/bin\/winspire --net-text /,/^fi$/d' "$work/root/netinit.sh"

sed -i \
	-e 's/echo "[Ww]inspire" > strings\/0x409\/manufacturer/echo "WiNspire" > strings\/0x409\/manufacturer/' \
	-e 's/echo "[Ww]inspire RNDIS Netdisk" > strings\/0x409\/product/echo "WiNspire RNDIS Netdisk" > strings\/0x409\/product/' \
	"$work/root/netinit.sh"

# Keep startup quiet so it does not overwhelm the user.
if ! grep -q '^WINSPIRE_QUIET_STARTUP=' "$work/root/netinit.sh"; then
	sed -i '2i\
WINSPIRE_QUIET_STARTUP="${WINSPIRE_QUIET_STARTUP:-1}"\
if [ "$WINSPIRE_QUIET_STARTUP" = 1 ]; then\
\texec 3>&1 4>&2\
\texec >>/tmp/winspire-startup.log 2>&1\
fi\
' "$work/root/netinit.sh"
	sed -i '/^say_fail() {$/,/^}$/c\
say_fail() {\
\tif [ "$WINSPIRE_QUIET_STARTUP" = 1 ]; then\
\t\texec 1>&3 2>&4\
\t\tWINSPIRE_QUIET_STARTUP=0\
\tfi\
\techo "[WiNspire] ERROR: $1"\
}' "$work/root/netinit.sh"
fi

sed -i '/^banner "USB ready; reconnect cable now only if Windows has not enumerated RNDIS"$/c\
if [ -w /dev/tty0 ]; then\
\tprintf '\''\\n[WiNspire] Unplug and reconnect USB now so Windows enumerates the RNDIS device.\\n'\'' > /dev/tty0\
else\
\techo "[WiNspire] Unplug and reconnect USB now so Windows enumerates the RNDIS device."\
fi' "$work/root/netinit.sh"

# Use a WRBP request to test the path that the emulator actually needs.
sed -i '/^banner "Waiting for server at 192.168.7.1"$/,/^say_ok "Network ready"$/c\
banner "Waiting for WiNspire server at 192.168.7.1"\
ok=0\
i=1\
while [ "$i" -le 30 ]; do\
\tif [ -x /bin/sync_server_time ] \&\& /bin/sync_server_time; then\
\t\tok=1\
\t\tbreak\
\tfi\
\tsleep 1\
\ti=$((i + 1))\
done\
if [ "$ok" -ne 1 ]; then\
\tsay_fail "WINSPIRE SERVER NOT REACHABLE AT 192.168.7.1:3860"\
\techo "Check that the server is running and Remote NDIS has 192.168.7.1/24."\
\texec /bin/sh\
fi\
say_ok "Server connected and guest clock synchronized"' "$work/root/netinit.sh"

# Restore the clock to 396 MHz after USB leaves it at 288 MHz.
sed -i '/^if \[ -x \/bin\/clock_restore \] && ! \/bin\/clock_restore; then$/,/^fi$/d' \
	"$work/root/netinit.sh"
sed -i '/^banner "Preparing USB network"$/i\
if [ -x /bin/clock_restore ] && ! /bin/clock_restore; then\
\tsay_fail "Calculator clock restore failed"\
fi' "$work/root/netinit.sh"

# Replace profile exports so an already-packaged donor produces the same output.
for variable in \
	WINSPIRE_PROFILE \
	TINY386_HEADLESS_REALTIME \
	TINY386_GUEST_CYCLE_SCALE \
	TINY386_IDLE_ASSIST \
	TINY386_BULK_REP_RAM \
	TINY386_BULK_REP_STOS \
	TINY386_HEADLESS_VGA_EVERY \
	TINY386_HEADLESS_VGA_FULL_EVERY \
	TINY386_HEADLESS_VGA_INPUT_EVERY \
	TINY386_HEADLESS_VGA_INPUT_BURST \
	TINY386_NET_FRAME_EVERY \
	TINY386_NET_TEXT_EVERY \
	TINY386_HEADLESS_PROGRESS_EVERY \
	TINY386_FB_MIRROR_DIAG_EVERY; do
	sed -i "/^export ${variable}=/d" "$work/root/netinit.sh"
done

# The touchpad can appear after init begins, so retry its I2C address before launch.
sed -E -i \
	-e '/^# Set Linux time before WiNspire builds the guest CMOS clock\.$/d' \
	-e '/^(if \[ "\$\{WINSPIRE_SYNC_SERVER_TIME:|banner "Synchronizing guest clock")/,/^banner "Launching WiNspire"$/d' \
	"$work/root/netinit.sh"
cat > "$work/startup-block" <<'EOF'
setup_touchpad() {
	driver=/sys/bus/i2c/drivers/nspire_captivate
	for attempt in 1 2 3 4 5; do
		for adapter in /sys/class/i2c-adapter/i2c-*; do
			[ -e "$adapter" ] || continue
			bus=${adapter##*i2c-}
			device_id="$bus-000a"
			device="/sys/bus/i2c/devices/$device_id"
			new_device="/sys/bus/i2c/devices/i2c-$bus/new_device"
			[ -w "$new_device" ] || new_device="$adapter/new_device"

			if [ ! -e "$device" ] && [ -w "$new_device" ]; then
				echo "nspire_captivate 0x0a" > "$new_device" 2>/dev/null || true
			fi
			[ -L "$device/driver" ] && return 0
			if [ -e "$device" ] && [ -w "$driver/bind" ]; then
				echo "$device_id" > "$driver/bind" 2>/dev/null || true
			fi
			[ -L "$device/driver" ] && return 0
		done
		sleep 1
	done
	echo "[WiNspire] touchpad driver did not bind" >&2
}
setup_touchpad
banner "Launching WiNspire"
EOF
awk -v block="$work/startup-block" '
	/^dmesg -n 1 / {
		while ((getline line < block) > 0) print line
		close(block)
	}
	{ print }
' "$work/root/netinit.sh" > "$work/netinit.sh"
mv "$work/netinit.sh" "$work/root/netinit.sh"
chmod 0755 "$work/root/netinit.sh"

configure_ini() {
	local target=$1
	local guest=$2
	local guest_cmos guest_cpu guest_fpu

	# SCALE_2_1 doubles the 320x240 host surface for the 640x480 VBE mode.
	case "$guest" in
		win9x)
			guest_cmos=1
			guest_cpu=4
			guest_fpu=0
			;;
		win2000|xpe)
			guest_cmos=0
			guest_cpu=5
			guest_fpu=1
			;;
	esac
	sed -E -i \
		-e "s/^mem_size = .*/mem_size = 44M/" \
		-e "s/^vga_mem_size = .*/vga_mem_size = 2M/" \
		-e "s/^fill_cmos = .*/fill_cmos = $guest_cmos/" \
		-e "s/^gen = .*/gen = $guest_cpu/" \
		-e "s/^fpu = .*/fpu = $guest_fpu/" \
		-e "s/^width = .*/width = 320/" \
		-e "s/^height = .*/height = 240/" \
		"$target"
}

mkdir -p "$work/root/configs"
for guest in win9x win2000 xpe; do
	cp "$work/root/winspire.ini" "$work/root/configs/$guest.ini"
	configure_ini "$work/root/configs/$guest.ini" "$guest"
done
cp "$work/root/configs/xpe.ini" "$work/root/winspire.ini"
sed -i 's#/bin/winspire /winspire.ini#/bin/winspire#g' "$work/root/netinit.sh"

find "$work/root" -exec touch -h -d '@0' {} +
(
	cd "$work/root"
	LC_ALL=C find . -print0 | LC_ALL=C sort -z |
		cpio --null -o -H newc --owner 0:0 --reproducible --quiet |
		gzip -9n >"$out_root/linux/nspire-linux-micro-initrd.tns"
)

install -m 0644 "$repo/calc/linuxloader2_auto.tns" "$out_root/linuxloader2_auto.tns"
LC_ALL=C sed -i \
	-e 's/Winspire/WiNspire/g' \
	"$out_root/linuxloader2_auto.tns"
install -m 0644 "$repo/calc/linux/dtb.tns" "$out_root/linux/dtb.tns"
install -m 0644 "$repo/calc/linux/zImage.tns" "$out_root/linux/zImage.tns"

rm -f "$out_root/README.txt" "$out_root/PROFILE.txt" \
	"$out_root/README-CALC-PROFILES.txt"

rm -rf "$work_root"
echo "packaged $out_root"
