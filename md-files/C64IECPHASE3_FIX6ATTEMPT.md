# C64 IEC / 1541 Phase 3 Fix Attempt 6 Hand-off

## Required outcome

Fix the genuine 1541 emulation path. Do not replace it with the older KERNAL `LOAD` trap.

The command:

```sh
./build/c64m -a --disk 8=./assets/disks/GALENCIA.D64
```

must visibly progress on the C64 screen from:

```text
SEARCHING FOR *
```

to:

```text
LOADING
```

and then return all the way to BASIC so the autorun machinery can type/execute:

```text
RUN
```

The current failure is that `LOADING` never appears. The machine remains at `SEARCHING FOR *`.

Manual reproduction is also:

```basic
LOAD"$",8
```

or:

```basic
LOAD"*",8
```

with `[disk] emulate_1541=1` and drive 8 mounted to a D64.

## Non-goals / rejected workaround

Do not solve this by keeping or re-enabling `c64_try_kernal_load_trap()` for devices whose 1541 ROM is loaded and `emulate_1541=1`.

That workaround makes standard KERNAL `LOAD` succeed through the old D64 backend, but it does not fix the problem. The requested behavior is successful load through the 1541 ROM / IEC path.

If you see a previous attempt that removed the trap bypass in `src/machine/c64.c`, treat that as a failed workaround, not an acceptable fix.

Also do not treat the host log line:

```text
RUN command received
```

as proof that BASIC saw `RUN`. That line refers to the runtime command/autorun machinery, not the C64 screen state.

## Documents to read first

Read these in order:

1. `md-files/C64IEC1541.md`
2. `md-files/C64IEC1541PHASE_1.md`
3. `md-files/C64IEC1541PHASE_2.md`
4. `md-files/C64IEC1541PHASE_3.md`
5. `md-files/C64IEC1541PHASE_3_Conversation.md`
6. `md-files/docs/status/IEC1541.md`
7. `md-files/docs/status/DISK_IO.md`
8. `md-files/C64CIAPHASE_A.md` through `md-files/C64CIAPHASE_K.md` as needed for CIA timing and IEC bus behavior.
9. `md-files/C64CLOCKSCHED.md` for machine/drive cycle scheduling assumptions.
10. `md-files/C64CPUVALIDATION.md` if CPU interrupt timing becomes suspect.

Useful source files:

1. `src/machine/c64.c`
2. `src/machine/c64.h`
3. `src/machine/c1541.c`
4. `src/machine/c1541.h`
5. `src/machine/via6522.c`
6. `src/machine/cia.c`
7. `src/runtime/runtime_thread.c`
8. `src/runtime/runtime.c`
9. `src/runtime/runtime_internal.h`
10. `tests/machine/test_c1541.c`
11. `tests/machine/test_c64_disk_load.c`
12. `tests/runtime/test_runtime_disk.c`

## Configuration context

The user's default `c64.ini` includes:

```ini
[roms]
character=roms/character.rom
system=roms/system.rom
1541=roms/1541.rom

[disk]
emulate_1541=1
```

The 1541 ROM is expected to be the 16 KB combined DOS 2.6 image:

```text
lower 8 KB = 325302-01
upper 8 KB = 901229-06AA
```

## Commands and running tips

Build:

```sh
cmake --build build
```

Focused tests:

```sh
./build/test_c1541
./build/test_c64_disk_load
./build/test_runtime_disk
```

Full test suite:

```sh
ctest --test-dir build --output-on-failure
```

Headless smoke run with dummy SDL drivers:

```sh
SDL_VIDEODRIVER=dummy \
SDL_AUDIODRIVER=dummy \
SDL_RENDER_DRIVER=software \
/opt/homebrew/bin/timeout 8 \
./build/c64m -a --disk 8=./assets/disks/GALENCIA.D64
```

The command timing out is normal because the emulator keeps running. A timeout does not prove success or failure. Host output saying `RUN command received` does not prove BASIC saw `RUN`.

The user is explicitly fine with launching the full SDL version on the desktop:

```sh
./build/c64m -a --disk 8=./assets/disks/GALENCIA.D64
```

If sandboxed SDL reports:

```text
SDL_Init failed: The video driver did not add any displays
```

then a desktop/unsandboxed run is needed to inspect the real screen.

## Good verification signals

The C64 screen must show `LOADING`. That is the key missing transition.

A correct run should look like:

```text
SEARCHING FOR *
LOADING
```

then eventually return to BASIC and autorun `RUN`.

For `LOAD"$",8`, a correct run should show:

```text
SEARCHING FOR $
LOADING
READY.
```

or equivalent BASIC-ready state after the directory program is loaded.

## Important trace findings from prior failed attempts

Prior instrumentation found this approximate flow before the hang:

1. The C64 sends LISTEN device 8.
2. The C64 sends secondary/name data for the requested load.
3. The C64 sends UNLISTEN.
4. The 1541 receives the initial LISTEN/name/UNLISTEN sequence.
5. The C64 then asserts ATN for TALK/TKSA.
6. The C64 ends up waiting in the KERNAL serial output/listener wait path, around the `$EDC7` / `$EDD6` area.
7. At the stuck point, the C64 is pulling DATA and waiting for the listener/drive handshake. The drive is not pulling the expected line.
8. The 1541 has not entered the talk sender path. Previous traces did not show the expected TALK 8 (`$48`) handling or talk sender labels being reached.

One trace observation was that after UNLISTEN the drive ROM was down in DOS open/read work when the C64 asserted ATN for the TALK/TKSA phase. The VIA CA1 interrupt did fire and the 1541 IRQ handler set the "ATN pending" style state, but the ROM returned to the interrupted DOS code instead of immediately servicing the serial ATN handler. By the time the drive returned to the serial idle path, the C64 side was already wedged waiting for the TALK/TKSA handshake.

This points to a timing/integration problem in the real 1541 path, not to the old D64/KERNAL trap.

## Failed ideas to avoid repeating blindly

### Re-enable KERNAL trap

Removing the `emulate_1541` bypass from `c64_try_kernal_load_trap()` makes `$FFD5` loads succeed through the mature D64 trap. This does not meet the requirement.

### Force the 1541 PC to the ATN handler

A previous probe forced the drive CPU PC to the ATN serial handler entry when ATN rose while the drive was outside the serial handler. This proved the timing suspicion but was not a valid fix: it abandoned the DOS open state and led to `?FILE NOT FOUND ERROR` rather than a successful load.

### Satisfy queued jobs earlier only

Another attempt called `c1541_satisfy_queued_jobs()` at extra DOS wait/post points around the open/read path. It did not make `LOADING` appear.

## Likely areas to investigate

Focus on the transition between:

1. C64 KERNAL load opening the file/channel over IEC.
2. 1541 DOS resolving/opening the file from D64-backed sector reads.
3. 1541 becoming ready to answer TALK for the load data.
4. C64 KERNAL receiving the first load byte and printing `LOADING`.

Specific places worth instrumenting:

1. C64 KERNAL IEC routines around `SETLFS`/`SETNAM`/`LOAD`, especially calls into LISTEN, SECOND, CIOUT, UNLSN, TALK, TKSA, ACPTR.
2. C64 CIA #2 Port A writes/reads (`$DD00`, `$DD02`) and the computed IEC open-collector pulls.
3. 1541 VIA #1 serial bus registers around `$1800`/`$1801`/`$1802`/`$1803`, CA1/IFR/IER state, and IRQ entry/return.
4. 1541 ROM serial handler around the ATN path and TALK sender path.
5. 1541 DOS open-file path after receiving `LOAD"*",8`, especially whether it returns to serial idle soon enough and whether the command/file channel state is valid.
6. Any C64/1541 cycle scheduling mismatch that lets the C64 move to TALK too aggressively before the 1541 can finish open processing.

## Suggested instrumentation strategy

Keep instrumentation temporary and remove it before finalizing unless it becomes a real diagnostic facility.

Useful trace files:

```text
/private/tmp/c64m-load-debug.log
/private/tmp/c64m-1541-trace.log
/private/tmp/c64m-c64-iec-trace.log
```

Useful environment variables for temporary probes:

```sh
C64M_LOAD_DEBUG=1
C64M_1541_TRACE=1
C64M_C64_IEC_TRACE=1
```

Suggested C64 trace fields:

1. C64 PC, A/X/Y/P/SP.
2. Current KERNAL IEC routine PC region.
3. CIA2 PRA/DDRA.
4. Computed C64 IEC pull mask.
5. Aggregated external drive pull mask.
6. Screen RAM lines so the trace distinguishes `SEARCHING`, `LOADING`, `READY`, and `RUN`.

Suggested 1541 trace fields:

1. Drive PC, A/X/Y/P/SP.
2. VIA #1 ORB/DDRB/input/IFR/IER/CA1.
3. Current C64 pull mask and aggregated bus-low mask.
4. Zero-page serial/DOS state bytes used by the 1541 ROM's ATN and talk/listen handlers.
5. Job queue bytes `$00` through `$05`.
6. Header/track-sector bytes `$06` through `$11`.
7. Buffer/channel metadata around the file/channel tables.

Keep the trace selective. Logging every instruction for both CPUs will produce huge logs and can perturb timing.

## Useful ROM labels / addresses already seen

These addresses came from earlier traces and source comments. Verify against the exact ROM image if needed.

```text
C64 KERNAL LOAD entry:        $FFD5
C64 KERNAL TALK/TKSA area:    around $F4CB, $F4D2, $EDC7, $EDD6
1541 physical read entry:     $F3B1
1541 reed sector read entry:  $F4CA
1541 read success return:     $F505
1541 job completion errr:     $F969
1541 serial/ATN area:         around $E850-$EA60
1541 IRQ entry path observed: around $FE67/$FE73 then serial IRQ helper around $E853
```

Previous traces suggested the drive eventually executes the IRQ helper that notices ATN, but it does not immediately service the TALK in a way that satisfies the C64. The exact ROM control flow after that point is the key.

## Source behavior to preserve

The D64 sector job intercept in `src/machine/c1541.c` is currently a pragmatic replacement for unmodelled physical disk/GCR mechanics. It should continue to satisfy physical READ/SEARCH-style jobs by copying sectors from the mounted D64 into 1541 RAM buffers.

The fix should make the ROM/DOS/IEC path coherent enough that the 1541 can complete the standard KERNAL load protocol. It does not need to implement full rotating disk mechanics if the existing job intercept remains sufficient.

## Acceptance checklist

Before declaring success:

1. Clean worktree except intended fix/docs/tests.
2. No temporary trace hooks or env-var probes left in production code unless intentionally documented.
3. `cmake --build build` passes.
4. `./build/test_c1541` passes.
5. `./build/test_c64_disk_load` passes.
6. `ctest --test-dir build --output-on-failure` passes.
7. Manual or screenshot-verified run of:

   ```sh
   ./build/c64m -a --disk 8=./assets/disks/GALENCIA.D64
   ```

   shows `SEARCHING FOR *`, then `LOADING`, then returns to BASIC and runs.
8. Manual or screen-verified `LOAD"$",8` shows `SEARCHING FOR $`, then `LOADING`, then returns to READY with the directory loaded.

Do not accept host-side autorun logs as a substitute for the C64 screen reaching `LOADING`.
