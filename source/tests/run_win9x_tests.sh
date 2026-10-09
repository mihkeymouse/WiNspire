#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
src=$repo/source/winspire
here=$repo/source/tests
out=$repo/build/tests/win9x
mkdir -p "$out"
core=(ini i386 fpu i8259 i8254 ide vga i8042 misc adlib ne2000 i8257 sb16 pcspk fmopl pc pci win32)
sources=()
for name in "${core[@]}"; do sources+=("$src/$name.c"); done
profile_flags=(-O2 -DWINSPIRE_SERVER_BUILD -include "$src/release_config.h" -I "$src" -ffunction-sections -fdata-sections -Wl,--gc-sections)
gcc "${profile_flags[@]}" "$here/frontend_policy.c" -lm -o "$out/frontend-policy"
"$out/frontend-policy"
arm-linux-gnueabi-gcc "${profile_flags[@]}" -static -marm -mcpu=arm926ej-s "$here/frontend_policy.c" -lm -o "$out/frontend-policy-arm"
qemu-arm "$out/frontend-policy-arm"
for target in native server; do
  define=WINSPIRE_SERVER_BUILD
  extra=()
  if [ "$target" = native ]; then define=WINSPIRE_NATIVE_BUILD; extra=(-DEXPECT_BATCH=1024 -DWINSPIRE_NATIVE_DIAGNOSTICS=1); fi
  flags=(-O2 -D"$define" -include "$src/release_config.h" -I "$src" -I "$here/platform-stub" -ffunction-sections -fdata-sections -Wno-unused-result)
  arm-linux-gnueabi-gcc "${flags[@]}" "${extra[@]}" -DEXPECT_YIELD -static -marm -mcpu=arm926ej-s "${sources[@]}" "$here/cpu_cadence.c" -Wl,--gc-sections -lm -o "$out/$target-arm"
  qemu-arm "$out/$target-arm"
done
gcc -O2 -fsanitize=address -fno-omit-frame-pointer -DWINSPIRE_SERVER_BUILD -include "$src/release_config.h" -I "$src" -ffunction-sections -fdata-sections "${sources[@]}" "$here/timer_cadence.c" -DEXPECT_LIVE -Wl,--gc-sections -lm -o "$out/timer-asan"
ASAN_OPTIONS=detect_leaks=0 "$out/timer-asan"
gcc -O2 -fsanitize=undefined "$here/native_clock.c" -o "$out/native-clock"
"$out/native-clock"
arm-linux-gnueabi-gcc -O2 -static -marm -mcpu=arm926ej-s "$here/native_clock.c" -o "$out/native-clock-arm"
qemu-arm "$out/native-clock-arm"
log_flags=(-O2 -DWINSPIRE_NATIVE_BUILD -DWINSPIRE_NATIVE_DIAGNOSTICS=1 -include "$src/release_config.h" -I "$src")
gcc "${log_flags[@]}" -fsanitize=undefined "$here/native_logging.c" -o "$out/native-logging"
(cd "$out" && ./native-logging)
arm-linux-gnueabi-gcc "${log_flags[@]}" -static -marm -mcpu=arm926ej-s "$here/native_logging.c" -o "$out/native-logging-arm"
(cd "$out" && qemu-arm ./native-logging-arm)
vga_flags=(-O3 -DWINSPIRE_NATIVE_BUILD -include "$src/release_config.h" -I "$src" -I "$here/platform-stub" -ffunction-sections -fdata-sections -Wl,--gc-sections)
for mode in fast generic; do
  extra=();if [ "$mode" = generic ]; then extra=(-DTEST_GENERIC);fi
  gcc "${vga_flags[@]}" "${extra[@]}" -fsanitize=address,undefined "$here/vga_planar.c" -o "$out/vga-$mode"
  ASAN_OPTIONS=detect_leaks=0 "$out/vga-$mode" "$out/vga-$mode.raw"
  arm-linux-gnueabi-gcc "${vga_flags[@]}" "${extra[@]}" -static -marm -mcpu=arm926ej-s "$here/vga_planar.c" -o "$out/vga-$mode-arm"
  qemu-arm "$out/vga-$mode-arm" "$out/vga-$mode-arm.raw"
done
cmp "$out/vga-fast.raw" "$out/vga-generic.raw"
cmp "$out/vga-fast-arm.raw" "$out/vga-generic-arm.raw"
cmp "$out/vga-fast.raw" "$out/vga-fast-arm.raw"
echo 'PASS: fast and generic planar pixels match on x64 and ARM926'
echo 'PASS: ARM926 Native/Server cadence and sanitizer timer waits'
