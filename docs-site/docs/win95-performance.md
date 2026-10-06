---
title: Windows 95 responsiveness fixes
---

Windows 95 could feel unresponsive in Native mode because WiNspire inherited a
65,536-instruction CPU batch, then checked input only every two batches. The
preserved nspire95 native build used 1,024-instruction batches. This branch restores
that interval, queues input before execution, and bounds native execution with a
20 ms host deadline checked every 64 ordinary instructions.

| Path | Before | Fixed |
| --- | --- | --- |
| Native input checks | Up to 131,072 ordinary instructions apart | Up to 2,048 apart |
| Native LCD/deadline clock | `clock()` backed by the installed SDK's `_times()` stub | Secondary SP804 hardware counter at 32,768 Hz |
| Server Win9x redraw/retrace poll | Every eight CPU batches | Every CPU batch and directly after input |
| Server Win9x timer reads | Frozen until the CPU batch returned | Consume CPU progress within the batch |
| Win9x REP memory operations with bulk shortcuts off | Could monopolize one batch | Yield between bounded chunks/page boundaries |
| Win9x graphics-to-text transition | Could hold the splash indefinitely | Four-refresh bound reveals DOS prompts |

The Native host counter handles wrap, ignores duplicate samples, and restores
timer load/control settings on normal exit. Guest time remains CPU-paced. The
installed Ndless library's `_times()` returns zero without populating its timing
structure; `_gettimeofday()` supplies only RTC seconds. The normal nspire95 source
also used `clock()` in its LCD helper; the SDK linked into older released binaries
was not inspected. The hardware counter comes from that source's experimental
timer path, and this fix uses it only for host scheduling.

Server batches remain 8,192 instructions. Win9x retains cycle scale 12, idle assist
off, and bulk MOVS/STOS shortcuts off. Windows 2000 and XPe runtime defaults are
preserved. A shared `runtime_policy.h` supplies Server and desktop-harness defaults,
recognizes Win95/98/Me/Win9x aliases and both path separators, and honors explicit
environment overrides.

The review covered native key/touchpad polling, Linux input queuing, PS/2 IRQ
edges, timer reads, retrace, LCD/mirror dirty updates, IDE caching/writeback and
host WRBP transport. No disk-server protocol change is part of this branch.
In local host tests, both old/current servers passed 1,000 single-sector reads and
100 write/readback pairs; mean loopback read latency was about 0.18 ms for both.
This does not measure calculator USB latency.

Run the deterministic regression checks with:

```bash
bash source/tests/run_win9x_tests.sh
```

They exercise the actual core and frontend clock: native batch size and deadline,
forward/reverse REP MOVS, STOS, an early-stopping comparison, 16-bit address sizes,
profile aliases/overrides and clock wrap/lifecycle. A PIT wait that consumed a whole
8,192-instruction batch now completes in 10 instructions. x64 sanitizer and ARM926
checks pass; the Native package builds with the installed Ndless SDK.

The standalone SDL harness shares Server profiles and input-before-execution
ordering:

```bash
bash source/tests/build_desktop_win9x.sh
build/tests/desktop-win9x path/to/win95.ini
```

Use a disposable disk copy for desktop comparisons. Set `bios`, `vga_bios` and
`hda` to host paths in the INI; matching 16 MiB RAM, 256 KiB VRAM and 320x240 output
make a useful Win95 fixture. The development comparison reached the same desktop
with the kept 6 MiB demo. The larger image reported a damaged ScanDisk log, and
automated GUI keyboard-latency measurements were inconclusive. No measured
calculator speedup is claimed. A physical CX II mouse/key/window-redraw retest
remains required.

The README, existing disk images and original nspire95 repository are unchanged.
The broader 1.1 development work remains in its original checkout.
