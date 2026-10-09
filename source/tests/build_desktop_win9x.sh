#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
src=$repo/source/winspire
out=${WINSPIRE_DESKTOP_OUTPUT:-$repo/build/tests/desktop-win9x}
mkdir -p "$(dirname -- "$out")"
core=(ini i386 fpu i8259 i8254 ide vga i8042 misc adlib ne2000 i8257 sb16 pcspk fmopl pc pci win32 osd/microui osd/osd)
sources=();for name in "${core[@]}"; do sources+=("$src/$name.c");done
read -r -a sdl_flags <<<"$(sdl-config --cflags)"
read -r -a sdl_libs <<<"$(sdl-config --libs)"
gcc -O3 -Wno-unused-result -DWINSPIRE_SERVER_BUILD -include "$src/release_config.h" -I "$src" \
    "${sdl_flags[@]}" "${sources[@]}" "$repo/source/tests/desktop_win9x.c" "${sdl_libs[@]}" -lm -o "$out"
echo "Built $out"
