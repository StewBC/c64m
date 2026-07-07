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

## TL;DR

- The reveal is a **cycle-exact "stable raster" kernel**, not an FLI/bad-line
  effect and not a game stall. The title copies a hand-cycle-counted raster
  splitter into RAM at `$0400-$0590` and drives the whole picture from it.
- The reveal itself is **per-line sprite-to-background priority (`$D01B`)
  animation**. A mask at `$CA00` sweeps `FF -> 00` (even entries top-down, then
  odd entries bottom-up) — exactly the "down then back up" motion VICE shows.
- **c64m runs the game logic perfectly** — the `$CA00` mask animates flawlessly
  frame by frame, and sprite priority is rendered live and correctly.
- **The defect is CPU<->VIC bus cycle-stealing not being cycle-exact.** The kernel
  is hand-counted against a real NTSC per-line cycle budget; c64m steals the wrong
  number of CPU cycles per line while sprites are fetching, so the per-line writes
  drift and the picture desyncs in the sprite-dense lower band.
- Fix surface is small and already in the machine layer: `c64.c` BA/RDY stall
  rule and `vicii.c` Phase-H sprite BA windows. **The renderer needs no changes.**

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

## Root cause

The kernel's correctness depends on the CPU consuming **exactly** the hardware
number of cycles per raster line, because that is what makes each `cpy $D012`
split and each hand-counted delay land on the intended raster/cycle. That budget
is `cycles_per_line - cycles_the_VIC_steals`. c64m's cycle-stealing model is not
hardware-exact in the sprite-dense region, so a small per-line error accumulates
and the writes desync where sprites are densest (the lower band).

This is the machine-layer bus timing, NOT the renderer. Two suspects:

```text
- Sprite-DMA BA windows: vicii.c Phase-H tables (vicii.c:89-101) and
  vicii_sprite_dma_next_line (vicii.c:961). Each active sprite must steal exactly
  its hardware cycles at the right line-relative cycle. This is the "NTSC sprite
  BA timing parity" item that AGENTS.md already lists as in-scope.
- RDY/AEC write tolerance: a real 6510 keeps WRITING for up to 3 cycles after BA
  drops and only stalls on the first read thereafter. c64_cpu_cycle_stalled_by_ba
  (c64.c:989) uses a read/write heuristic that may not match the exact 3-write
  rule. $D011/$D01B are stores, so this directly moves when the kernel's writes
  land. (AGENTS.md currently lists exact RDY/AEC sub-cycle timing as out of scope;
  fixing this title may require pulling it in.)
```

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
