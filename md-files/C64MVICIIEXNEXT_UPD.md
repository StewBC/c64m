# C64MVICIIEXNEXT_UPD — dkarcade "expose" reveal: mechanism CONFIRMED + fix plan

This supersedes the "Hypotheses for the real mechanism" section of
[C64MVICIIEXNEXT.md](C64MVICIIEXNEXT.md). The reveal mechanism is no longer a
hypothesis — it has been traced end to end (game code disassembled, register
writes traced, output localised). Read alongside:

```text
md-files/C64MVICIIEXNEXT.md                 prior handoff (hypotheses now resolved)
md-files/docs/status/VICII_EXPOSE_REVEAL.md original investigation (mechanism corrected)
src/machine/vicii.c                         renderer + sprite BA windows (Phase H)
src/machine/c64.c                           CPU<->VIC bus cycle-stealing (BA/RDY/AEC)
```

## STATUS (2026-07): NTSC CLOSED, PAL is the open bug

> **NTSC is RESOLVED — c64m is correct.** A VICE NTSC capture confirmed the
> ~27-frame "plateau" is **intended hardware behaviour**, not a defect: VICE shows
> the same hold. Both Phase A "open questions" below are therefore answered
> (Q1: the plateau is NOT a defect; Q2: no fix needed). No NTSC code change is
> required. The historical NTSC investigation is retained below for the record.
>
> **The live bug is the PAL reveal** — a genuinely broken, separate issue with its
> own full diagnosis at the end of this document (see
> "PAL reveal corruption (dkarcade2016)"). Start there.

## TL;DR (NTSC — historical; see STATUS above)

- The reveal is a **cycle-exact "stable raster" kernel**, not an FLI/bad-line
  effect and not a game stall. The title copies a hand-cycle-counted raster
  splitter into RAM at `$0400-$0590` and drives the whole picture from it.
- The reveal itself is **per-line sprite-to-background priority (`$D01B`)
  animation**. A mask at `$CA00` sweeps `FF -> 00` (even entries top-down, then
  odd entries bottom-up) — exactly the "down then back up" motion VICE shows.
- The `$CA00` reveal mask animates flawlessly frame by frame, and sprite priority
  is rendered live and correctly.
- ~~The defect is CPU<->VIC bus cycle-stealing not being cycle-exact.~~
  **REFUTED BY PHASE A** — measurement shows the per-line cycle budget is correct
  and the kernel's writes land at stable cycles top-to-bottom, with no divergence
  at the frozen boundary. Bus timing and the renderer are both exonerated.
- **Phase A finding:** the reveal is a per-line `$D011` toggle (`$33` = visible
  bitmap, `$73` = ECM+BMM invalid = black). It DOES animate in c64m; the plateau
  is the per-line `$D011` VALUES freezing for ~27 frames while the mask keeps
  advancing — a stall in the game's reveal STATE MACHINE, not in timing/rendering.
- **RESOLVED:** a VICE NTSC capture confirmed the 27-frame hold is intended
  hardware behaviour, so c64m NTSC is correct and no fix is needed (see STATUS).

## The mechanism (traced, not hypothesised)

Disassembly of the RAM kernel (`da65` on a live dump of `$0400`):

```asm
L0418:  ldy $D012          ; wait for raster $2E (46)
        cpy #$2E
        bne L0418
        lda #$41
        sec
        sbc $DD04          ; A = $41 - CIA#2 Timer-A low  <- measures entry jitter
        sta L0429          ; SELF-MODIFY the BPL operand below
        .byte $10          ; BPL <computed> into a sled of `cmp #$C9` (2-cycle NOPs)
        ...                ; the sled cancels the raster-entry jitter to the cycle
L04AC:  cpy $D012          ; busy-wait to an exact raster
        beq L04AC
        sta $D011          ; per-line $D011 from $CA00,y  (values $73/$33)
        lda $CA00,y
        iny
        jsr L00F9          ; hand-counted inter-line delay
        ...                ; loop repeats down the screen until y reaches $FA (250)
L0474:  sta $D01B          ; per-line sprite-BG priority from $CA01,y  (FF/00 mask)
```

What it does, in order:

```text
1. Waits for raster $2E, reads CIA#2 Timer-A ($DD04), and branches into a
   `cmp #$C9` NOP-sled by ($41 - $DD04) cycles. Classic stable-raster jitter
   correction: turn a variable interrupt-entry cycle into a constant one.
2. Walks down the screen with `cpy $D012 / beq` raster busy-waits plus
   hand-counted `jsr` delays.
3. Writes $D011 (the $73/$33 = RST8 + RSEL bits that re-arm the raster-IRQ chain)
   and $D01B (the FF/00 sprite-to-background priority mask) per raster line from
   the $CA00 table.
```

The reveal = animating the per-line `$D01B` mask. Where a line's mask is `FF` the
sprites sit behind the bitmap; where it flips to `00` the priority inverts and the
line "opens". The game fills the mask a little more each frame, so the picture
wipes in.

## Evidence chain (why this is certain)

```text
- $CA00 reveal buffer animates perfectly every frame in c64m: FF->00, even
  entries top-down then odd entries bottom-up (matches the VICE description).
  => the game's reveal engine runs correctly on c64m.
- Across the ~27-frame plateau the RENDERED frame is byte-identical, yet the
  buffer keeps advancing. => c64m is not applying the buffer's per-line effect.
- Register-write trace of two plateau frames (272 vs 290): the VIC writes are
  VALUE-identical, only jittered by a few CPU cycles. => the game emits the same
  writes; the difference that should reveal lines is being lost in WHEN they land.
- Output localisation: the frozen band is exactly odd rasters 171..251 — the
  lower, sprite-multiplexed region. A uniform timing error would shift the whole
  picture; a band that desyncs only past mid-screen is a PER-LINE cycle error
  accumulating down the kernel's walk.
- Renderer is exonerated: sprite_priority ($D01B) is read live per pixel in
  vicii_compose_pixel (vicii.c:853/863) — a mid-line write takes effect at the
  exact pixel, nothing latched.
- Memory diff across the plateau: nothing changes except the $CA00 mask and
  zero-page scratch (sprite data/pointers/colour RAM all frozen).
```

## Root cause — REVISED BY PHASE A (see "Phase A RESULTS" below)

> **The bus-timing hypothesis in this section was NOT supported by Phase A
> measurement.** c64m's per-line cycle budget through the kernel is clean and the
> kernel's writes land at stable cycles top-to-bottom, with no divergence at the
> frozen boundary. The suspects below (sprite-DMA BA, RDY/AEC) are therefore NOT
> the cause of this title's plateau. Kept for the record; superseded by Phase A.

The original hypothesis (retained for context): the kernel is hand-cycle-counted,
so if c64m stole the wrong number of CPU cycles per line the per-line writes would
desync. Suspected: sprite-DMA BA windows (vicii.c Phase-H, `vicii_sprite_dma_next_line`)
and the RDY/AEC 3-write-cycle rule (`c64_cpu_cycle_stalled_by_ba`, c64.c:989).
Phase A measured both and found the budget correct — see below.

## Phase A RESULTS (measured — DONE)

Method: temporary per-cycle instrumentation in the `c64.c` step loop (count CPU
`exec` vs BA `stall` cycles per raster line) and in `vicii_write_register` (log
the cycle-in-line of every `$D011`/`$D01B` write), gated to specific frames,
driven headless over the control port. All instrumentation has been removed; tree
clean, 40/40 tests green.

Per-line cycle budget (frame 280, a plateau frame):

```text
- total = 65 every line (VIC clock is correct; no missing/extra cycles).
- Sprite DMA steals 18 cyc/line at EXACTLY the three sprite strips only:
  rasters 0-8 (sprite Y=0), 29-50 (Y=29), 250-262 (Y=250). Matches the documented
  sprite Y positions. The strips steal correctly.
- Bad lines steal 43 cyc (exec=22) at raster == 3 (mod 8), uniform top to bottom.
- The frozen band (odd rasters 171-249) has NO sprite steal at all -- it is NOT
  "sprite-dense". There is no per-line stolen-cycle error there.
- The kernel's $D011 write cycle-in-line is stable and periodic (~cyc 8-15) from
  raster 45 to 250, with NO discontinuity at raster 171.
```

Conclusion: **the CPU/VIC bus cycle-timing is not the defect.** No accumulating
error, no per-line budget divergence, no sub-line write drift at the boundary.
Phase B as written (fix BA/RDY stall) would not change this title.

What the reveal actually is (from the write-value trace):

```text
- The kernel writes $D011 per raster line: $33 = valid MCM bitmap (line VISIBLE),
  $73 = ECM+BMM invalid mode (line BLACK). Even rasters carry the picture; odd
  rasters are blanked with $73. (The $D01B/mask writes are a separate concern and
  fall on rasters with no active sprites, so they do not drive the bitmap band.)
- The reveal DOES animate in c64m: comparing a revealing frame (250) to a later
  one (280), even rasters 220-250 flip $73 -> $33 (more lines revealed). The
  engine progresses.
- The plateau = the per-line $D011 VALUES stop changing for ~27 frames (identical
  output) even though the underlying $CA00 mask keeps advancing. The stall is in
  the game's reveal state machine -- the step that turns the advancing mask into
  new active $D011 values (the "even lines done -> start odd lines" transition) --
  NOT in timing and NOT in rendering.
```

### Two open questions Phase A raised — BOTH NOW ANSWERED

```text
1. Is the plateau even a defect?  ANSWERED: NO. A VICE NTSC capture shows the same
   ~27-frame hold, so it is intended hardware behaviour. c64m NTSC is correct.
2. If it were a defect, where is the fix?  MOOT -- there is no NTSC defect, so no
   fix. (The speculative "reveal state machine" lead was never needed.)
```

## Fix plan (phases) — OBSOLETE for NTSC (no defect); see PAL section for the live bug

> Phase A moved the target off bus timing, and the VICE NTSC capture then showed
> there is **no NTSC defect at all** (the hold is intended). Nothing below needs to
> be implemented for NTSC. Phase B (BA/RDY stall) is a rejected lead. The historical
> plan is retained only for context; the live work is the PAL bug documented at the
> end of this file.

## Fix plan (phases)

Goal: c64m renders dkarcade2016's reveal identically to hardware by making the
CPU stall for exactly the cycles the VIC steals, on exactly the right cycles,
during sprite DMA and bad lines. The renderer is not touched.

### Phase A — Localise the exact off-by-N (measurement, no code change)

```text
- Instrument c64m to log, per raster line across the kernel's walk (~raster
  50..251), the number of cycles the CPU actually executed vs the number it was
  BA-stalled (c64.c step loop already distinguishes these).
- Compute the hardware budget from the Bauer VIC-II timing tables for NTSC
  6567R8: 65 cycles/line, minus 40 for a bad line, minus 2 per active sprite at
  the documented s-access/p-access positions.
- Diff per line. The FIRST raster where the cumulative CPU-cycle count diverges
  from the Bauer budget is the bug site; the instruction executing there names
  the subsystem (sprite DMA stall vs RDY/AEC write tolerance).
```

Acceptance:

```text
- A per-line "c64m executed / c64m stalled / hardware expected" table for the
  kernel region, with the first divergent raster and cycle identified and
  recorded in this document.
```

Risk: none (measurement only).

### Phase B — Correct the specific stall rule

```text
- If sprite DMA: fix the per-sprite BA window in vicii.c Phase-H so each active
  sprite steals exactly its hardware cycles at the correct line-relative cycle,
  including the cross-line case (vicii_sprite_dma_next_line).
- If RDY/AEC: implement the exact rule in c64_cpu_cycle_stalled_by_ba — the CPU
  continues writes for up to 3 cycles after BA goes low and stalls on the first
  read once BA has been low for >= 3 cycles.
- Keep the change minimal and targeted at the divergence found in Phase A; do not
  rewrite the bus model wholesale.
```

Acceptance:

```text
- The per-line cycle budget from Phase A now matches the Bauer numbers across the
  whole kernel region.
- The previously frozen plateau frames are no longer byte-identical; the lit-row
  metric advances during them (reuse the count_lit_rows harness).
- Full suite stays green, especially the existing PAL/NTSC sprite-BA and
  open-border regressions.
```

Risk: high — touches the same BA/stall path as the sprite-BA tests. Guardrails
first.

### Phase C — Verify against a hardware oracle and lock it

```text
- Confirm the reveal matches a cycle-exact reference (VICE, or a real-hardware
  capture) across the ~27 plateau frames — down then back up, no plateau.
- Add a regression test that runs the kernel region and asserts the per-line
  CPU-cycle budget equals the hardware value, so this cannot silently rot.
- Update AGENTS.md scope if the RDY/AEC rule was pulled in; update
  docs/status/VICII.md, DEFERRED.md, TESTING.md, and STATUS.md.
```

Acceptance:

```text
- Reveal visually matches the oracle within tolerance.
- Per-line cycle-budget regression test green; full suite green.
- Docs and scope reconciled.
```

Risk: low once Phase B lands (validation + documentation).

## What is NOT the problem (do not re-investigate)

```text
- Bad lines / FLI: YSCROLL is constant at 3; the reveal is not YSCROLL-driven.
  Phases 1-5 (per-scanline bad-line accuracy) are correct work but orthogonal.
- Sprite Y-expansion "crunch": $D017 is 0 for all sprites the whole frame.
- Game logic / a spin-wait on a wrong register read: the game runs perfectly; the
  $CA00 mask animates exactly right in c64m.
- The renderer / sprite-priority mux: $D01B is honoured live per pixel.
```

## Diagnostic tooling that worked (reuse for Phase A/C)

```text
- Headless control port for deterministic capture: run + `wait-frame N 90000`
  (explicit long timeout) + `get-frame`/`get-memory` is byte-reproducible within
  a build. get-memory caps at 1024 bytes/call (chunk larger reads).
- Register-write trace: a temporary `fprintf` in vicii_write_register gated on
  `v->timing.frame_number == <f>` logging (frame, raster, cycle, reg, value).
- Live disassembly: dump code with get-memory (mode ram) and run `da65
  --start-addr 0x0400 dump.bin`. The RAM kernel lives at $0400-$0590.
- Output localisation: count lit pixels per raster row (x in [24,344)) and diff a
  plateau frame against a settled frame to see WHICH rasters are frozen.
- Scratchpad capture scripts from the diagnosis session are session-temporary;
  recreate from the snippets in C64MVICIIEXNEXT.md if needed.
```

---

# PAL reveal corruption (dkarcade2016) — separate bug, deep diagnosis

NTSC is CLOSED (c64m matches VICE, including the ~27-frame hold — it is intended
hardware behaviour, not a defect). The PAL reveal, however, is genuinely broken in
c64m and correct in VICE. This section records the full diagnosis.

## Symptom

PAL only: the "expose" reveal shears diagonally and the bitmap corrupts during the
opening. The SETTLED picture is correct; only the reveal DYNAMICS are wrong. VICE
PAL reveals cleanly.

## Mechanism (traced)

The reveal's stable-raster kernel ($0400-$0590, entered once/frame via a raster IRQ,
then `cpy $D012`-polls down the screen writing $D011/$D01B and multiplexing sprite Y)
DESYNCS in c64m PAL. Its per-line line-cursor `Y` races ~3x ahead of the real raster,
so the sprite-Y multiplex writes land at the wrong rasters, sprites stop matching
their Y (0 active frames after the first few), stop stealing cycles, and the picture
shears. NTSC's longer line (65 cyc) absorbs the same error; PAL (63 cyc) does not.

## Cycle-exact VICE ground truth (x64sc, store/load watchpoints)

```text
- Sprite-Y multiplex ($D001), VICE PAL, STABLE every frame:
    $D001 <- 250  at raster 53, cycle 29
    $D001 <-  29  at raster 272, cycle 38
  c64m PAL: first write ~aligned (raster 52), second write drifts to raster
  ~124-173 and jitters -> the kernel reaches it ~145 lines early (3x race).
- CIA#2 Timer-A read ($DD04) at the jitter correction (PC $0422, raster 29):
    VICE: val+cyc == 74 for every jitter sample (cyc 13-21).
    c64m: val+cyc == 74 (cyc 14, val $3C).  => $DD04 phase is CORRECT.
```

## Ruled OUT (each by direct measurement or empirical fix)

```text
- Detection / wrong table: DISPROVEN. The game correctly detects PAL and installs
  the PAL table. $0318/$0501/$0419 and the whole kernel $0400-$0590 are
  BYTE-IDENTICAL between c64m PAL and VICE PAL (and differ from c64m NTSC).
- CIA-timer vs raster phase: DISPROVEN. $DD04 val+cyc==74 matches VICE exactly.
- $D012 read value/timing: correct. Projecting the raster read to the true read
  cycle (c64_cpu_read DEFER path) was implemented and verified active (d=3) but the
  read value was already right (reads land mid-line); no effect on the drift.
- Bad-line stall length: giving back the 3-cycle lead-in changed nothing.
- Sprite BA cycle-stealing: works when sprites are active (frame 138 steals 18/line).
- Sprite Y-match model, PAL geometry constants (63/312), 9-bit raster reads,
  CIA stepping (advanced every cycle): all correct.
```

## Remaining root (unresolved)

Identical kernel code + correct tables + correct $DD04 + correct $D012 reads, yet the
first multiplex write lands ~74 cycles off and cascades. The divergence is in the
EXACT cycle-by-cycle execution of the stable-raster entry/sled sequence (raster
29 -> first write). Prime suspect: the deferred CPU model detects IRQs at instruction
boundaries rather than the hardware's "2 cycles before instruction end" sampling,
giving a different entry-jitter that the $DD04 correction cannot fully cancel. This is
FLI-class sub-cycle timing, which AGENTS.md lists as out of scope.

## Next step if pursued

Instruction-by-instruction cycle trace of the ~23-line stretch (raster 29 -> first
$D001 write) in BOTH VICE (monitor trace) and c64m, aligned at the $DD04 read, to
find the exact instruction where the cycle counts diverge. Only then change the CPU
IRQ-timing / entry model, validating against the PAL reveal AND all 40 tests AND the
working NTSC reveal.

## Tooling (reusable)

```text
- VICE PAL driver: x64sc -pal -autostartprgmode 1 -autostart <prg>
    -remotemonitor -remotemonitoraddress ip4://127.0.0.1:<port>
  (the .prg is a 64K IRQ-hooked one-loader: autostart mode 1 = inject to RAM.)
- Watchpoints: `break store $d001` / `break load $dd04`; the hit line reports
  raster/cycle; read the value with `m <addr> <addr>` (skip the echoed address when
  parsing!). Byte dumps: `m <a0> <a1>`.
- c64m PNG capture: get-frame format=argb8888 -> pure-python PNG writer (scratchpad
  pnglib.py). PAL frame is 384x272.
```
