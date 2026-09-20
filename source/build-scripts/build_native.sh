#!/usr/bin/env bash
set -euo pipefail

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=${1:-$(cd -- "$script_dir/../.." && pwd)}
default_core=$repo/source/winspire
core=${2:-$default_core}
default_frontend=$repo/source/winspire-ndless/main.c
[ -f "$default_frontend" ] || default_frontend=$repo/source/native/main.c
frontend=${3:-$default_frontend}
sdk=${NDLESS_SDK:-$HOME/Ndless/ndless-sdk}
build=${4:-$repo/build}
work=$build/native-work
out=$build/Native
version=${WINSPIRE_VERSION:-1.0.0}
export PATH="$sdk/bin:$PATH"

for path in "$core/i386.c" "$core/pc.c" "$frontend" \
	"$sdk/bin/nspire-gcc" "$sdk/bin/nspire-ld" \
	"$sdk/bin/genzehn" "$sdk/bin/make-prg"; do
	[ -e "$path" ] || { echo "missing required path: $path" >&2; exit 1; }
done

rm -rf "$work" "$out"
mkdir -p "$work/obj" "$out"

cc=$sdk/bin/nspire-gcc
ld=$sdk/bin/nspire-ld
common_flags=(
	-std=c99 -marm -mcpu=arm926ej-s -mtune=arm926ej-s -Os -g -Wall
	-Wno-format -Wno-unused-function -Wno-unused-variable -I"$core"
	"-ffile-prefix-map=$repo/="
	"-fdebug-prefix-map=$repo/="
	"-fmacro-prefix-map=$repo/="
	-ffunction-sections -fdata-sections
	-DWINSPIRE_NATIVE_BUILD -include "$core/release_config.h"
	-DTINY386_INPUT_POLL_LOOPS=2U
	-DTINY386_VIDEO_POLL_LOOPS=1U
	"-DTINY386_VERSION=\"$version\""
)
hot_flags=(-O3 -fomit-frame-pointer)
core_sources=(
	ini.c i386.c i8259.c i8254.c ide.c vga.c i8042.c misc.c i8257.c
	pcspk.c adlib.c ne2000.c sb16.c pc.c pci.c
)
objects=()

"$cc" "${common_flags[@]}" "${hot_flags[@]}" -c "$frontend" \
	-o "$work/obj/nspire_main.o"
objects+=("$work/obj/nspire_main.o")

for source in "${core_sources[@]}"; do
	object="$work/obj/${source%.c}.o"
	extra=()
	case "$source" in
		i386.c|pc.c|vga.c|ide.c|i8254.c|i8042.c|i8259.c|misc.c)
			extra=("${hot_flags[@]}")
			;;
	esac
	"$cc" "${common_flags[@]}" "${extra[@]}" -c "$core/$source" -o "$object"
	objects+=("$object")
done

"$ld" "${objects[@]}" -o "$work/winspire.elf" -Wl,--gc-sections -lm
"$sdk/bin/genzehn" --input "$work/winspire.elf" \
	--output "$work/winspire.tns.zehn" \
	--name "WiNspire $version" \
	--author "Malik Idrees Hasan Khan" \
	--notice "x86 emulator for the TI-Nspire CX II" \
	--ndless-min 42 --ndless-rev-min 2004 --uses-lcd-blit 1
"$sdk/bin/make-prg" "$work/winspire.tns.zehn" "$out/winspire.tns"
rm -f "$work/winspire.tns.zehn"

install -m 0644 "$core/bios.bin" "$out/bios.bin.tns"
install -m 0644 "$core/vgabios.bin" "$out/vgabios.bin.tns"
sed 's/^hda = .*/hda = disk.img.tns/' \
	"$repo/source/winspire/native.ini.tns" > "$out/winspire.ini.tns"

rm -rf "$work"
echo "built $out"
