# C64MVICIIEXNEXT — dkarcade "expose" reveal: handoff and next steps

This is the working handoff for the per-scanline VIC-II accuracy effort and the
`samples/dkarcade2016.prg` open-border "expose" reveal. Read alongside:

```text
md-files/C64MVICIIEXPHASES.md              the phase plan + per-phase status log
md-files/docs/status/VICII_EXPOSE_REVEAL.md the original investigation (see caveat below)
md-files/docs/status/VICII.md               VIC-II component handoff
src/machine/vicii.c                         the renderer + sequencer (source of truth)
```

## TL;DR

- Phases 1-5 of the phase plan are implemented, unit-tested, regression-clean
  (full suite 40/40), and committed. They made the VIC renderer **counter-driven,
  latch-based, sequencer-idle, and per-cycle bad-line accurate** -- real accuracy
  gains, worth keeping on their own.
- **They did not fix the reveal.** A register trace proved the reveal is **not** a
  YSCROLL/bad-line (FLI) effect: YSCROLL is constant at 3 for the whole frame. The
  premise behind Phases 2-5 (and behind `VICII_EXPOSE_REVEAL.md`) does not apply
  to this title.
- Next work must **re-diagnose the real mechanism against a VICE reference**, not
  add more bad-line accuracy.

> **UPDATE (mechanism now CONFIRMED — see
> [C64MVICIIEXNEXT_UPD.md](C64MVICIIEXNEXT_UPD.md)):** the reveal has since been
> traced end to end. It is a **cycle-exact "stable raster" kernel** in RAM at
> `$0400-$0590` that animates per-line **sprite-to-background priority (`$D01B`)**
> from a mask at `$CA00`. The game logic runs perfectly on c64m; the defect is
> that c64m's **CPU<->VIC bus cycle-stealing is not hardware-exact**, so the
> kernel's hand-counted per-line writes desync in the sprite-dense lower band. The
> "Hypotheses" section below is resolved by that finding; the fix plan lives in
> `C64MVICIIEXNEXT_UPD.md`.

## What the phases changed (already committed)

See `C64MVICIIEXPHASES.md` for the detailed log. In short, in `src/machine/vicii.c`:

```text
- Phase 1: per-scanline test harness in tests/machine/test_c64_vicii.c
  (run_vic_frame_with_injections, count_lit_rows, expose_injection).
- Phase 2: address generation is counter-driven (VC/RC/VCBASE) via vicii_line_ctx,
  not raster geometry. Fixed a latent VCBASE +320/row bug (now +40/row, Bauer).
- Phase 3: the display reads character/colour from the video_matrix[]/color_line[]
  latch filled atomically at the bad line (vicii_fill_line_latch), not live RAM.
- Phase 4: idle vs display is chosen by sequencer display state (idle_when_inactive),
  not the fixed 51..251 window (snapshot/debug path unchanged).
- Phase 5: Bad Line Condition evaluated EVERY cycle, so a mid-line $D011 write can
  force a bad line on its own line. v->bad_line is the per-line "committed" latch.
```

All of these keep the snapshot/debug renderer path byte-identical and preserve the
open-border / DEN / sprite-BA regressions.

## The decisive finding: the reveal is not bad-line driven

Method: a temporary trace in `vicii_write_register` logging `(frame, raster,
cycle, reg, value)` for `$D011/$D016/$D018`, gated to one plateau frame
(frame 280), driven headless over the control port. Trace has been reverted.

Result for one plateau frame:

```text
- $D011 writes: 100x $33, 101x $73, 1x $3b, 1x $7b.
    $33 = YSCROLL=3, RSEL=0, DEN=1, BMM=1, ECM=0, RST8=0
    $73 = same, but RST8=1   ($3b/$7b = same with RSEL=1)
  => YSCROLL is CONSTANT at 3. The per-line writes only toggle bit 7 (RST8), i.e.
     they are scheduling the next raster IRQ, not manipulating bad lines/YSCROLL.
- $D016 / $D018: essentially static (a couple of writes). Display mode is a fixed
  multicolour bitmap the whole frame.
```

Implications:

```text
- The reveal is NOT the FLI/bad-line effect VICII_EXPOSE_REVEAL.md hypothesised.
  Its "Actual mechanism" section is contradicted by the YSCROLL-constant trace and
  should be corrected once the true mechanism is known.
- Phases 2-5 (per-scanline bad-line accuracy) cannot move this title, and indeed a
  Phase-4-vs-Phase-5 sweep is byte-identical.
- Good news: the reveal ALREADY largely animates in c64m. A deterministic sweep of
  consecutive frames shows lit rows growing (e.g. ~123 -> ~194) frame by frame; the
  only defect is a ~27-frame plateau (measured frames 270-297: one repeated hash,
  lit=194) where hardware keeps changing but c64m holds.
```

## What was tried

```text
- Full Phases 1-5 (above). Each has a deterministic unit test; none affect the
  plateau.
- Deterministic control-port capture of single settled frames (byte-identical
  across two runs) -> proves the capture method is reproducible.
- Consecutive-frame sweep across the reveal (frames ~220-300) on both Phase 4 and
  Phase 5 builds -> identical; plateau at 270-297 in both.
- Register-write trace of a plateau frame -> YSCROLL constant, RST8 toggling.
- Attempted cross-build pixel diff vs the pre-refactor baseline (git worktree at
  bb08f3d). Abandoned: the older build's wait-frame stopped early (boot frame), so
  frames were not comparable. Same-build comparisons (git stash) are reliable.
```

## Hypotheses for the real mechanism — RESOLVED

**These hypotheses are now settled.** The mechanism was traced end to end (game
code disassembled, register writes traced, output localised). The full confirmed
diagnosis and fix plan are in
[C64MVICIIEXNEXT_UPD.md](C64MVICIIEXNEXT_UPD.md). Outcome of each old candidate:

```text
1. Sprite multiplexing timing -> PARTIALLY RIGHT (it IS the sprite region), but
   the mechanism is not MCBASE. The reveal animates per-line sprite-to-background
   priority ($D01B). The real defect is CPU<->VIC bus cycle-stealing (sprite-DMA
   BA windows / RDY-AEC) not being cycle-exact, so the kernel's hand-counted
   per-line writes desync where sprites are dense (odd rasters 171..251).
2. Raster-IRQ scheduling precision -> NOT the cause. The reveal is driven by a
   cycle-exact stable-raster kernel in RAM ($0400-$0590) using `cpy $D012` busy
   waits and CIA#2 Timer-A ($DD04) jitter correction, not by raster-IRQ landing
   times. (The $D011 RST8 toggles only re-arm the IRQ chain.)
3. Per-frame VIC state carry -> NOT the cause. Nothing carries across frames
   except the game's own $CA00 mask; the game logic runs perfectly on c64m.
4. Genuinely game-driven / sprite-strip animation -> RULED OUT. The reveal is a
   per-line priority effect over the bitmap band, and the game DOES advance every
   frame (the $CA00 mask fills FF->00 exactly as VICE reveals).
```

The remaining work is a **machine-layer bus-timing fix** (make CPU cycle-stealing
hardware-exact in the sprite region), scoped as its own vertical slice with tests
in `C64MVICIIEXNEXT_UPD.md`. The renderer needs no changes. A VICE (or
real-hardware) capture is still useful, but only as the Phase-C verification
oracle — it is no longer needed to diagnose.

## Control-port ("remote API") learnings

The headless control server is the way to drive the emulator for these checks.
Source: `src/control/control_protocol.c`, `src/control/control_server.c`.

### Wire protocol

```text
- Launch: ./build/c64m --headless --control-port <PORT> --ntsc -p <file.prg>
    --headless REQUIRES --control-port. The process runs until quit; time-limit or
    kill it from the script (do not launch without a client or a timeout).
- Request line:  "<id> <command> [args]\n"   (id = decimal, echoed in the response)
- Response line: "<id> ok [text]\n"  |  "<id> error <text>\n"  |
                 "<id> data <type> <payload_size> [metadata]\n"
- For a DATA response, the payload_size bytes follow the line, then a trailing '\n'.
  Read the line, parse payload_size, read exactly that many bytes, then read 1 more.
```

### Commands that matter here

```text
- run                      start free-running.
- wait-frame <N> [<ms>]    wait for N MORE frames, then (deferred) reply. PASS AN
                           EXPLICIT timeout_ms (e.g. 90000); the default is short
                           and the deferred reply times out ("error ... deferred
                           response timed out") even though execution continued.
- get-frame format=argb8888  DATA payload = 384*263*4 bytes ARGB8888 (NTSC 263
                           lines). Metadata includes frame=<n> cycle=<n>.
- get-memory <addr> <len> [map|ram|rom]   read memory.
- get-state / get-cpu      machine + CPU state (frame number, raster, etc).
- run-cycles <N>           SEE GOTCHA below.
```

### Gotchas (cost real time here)

```text
- DETERMINISM: "run" then "wait-frame N 90000" then "get-frame" is deterministic
  within a build -- two runs gave byte-identical frames and the same frame number.
  Use this for reproducible captures. Free-running + polling is NOT reproducible
  (wall-clock dependent, and the title animates sprites every frame).
- run-cycles is unreliable for exact advancement: requesting ~5.4M cycles left the
  machine at ~2.07M (still on the boot screen). It appears to queue/coalesce and
  the get-frame raced ahead. Prefer wait-frame for advancing a known amount.
- CROSS-BUILD comparison is fragile: an older build (bb08f3d) returned wait-frame
  early at a boot frame while the current build reached the target frame, so the
  captured frames were not comparable. To compare code variants, stay in ONE build
  tree: `git stash` the change, rebuild, capture, `git stash pop`, rebuild.
- The reveal/boot timing: after "run", boot (blue border) lasts ~120 frames; the
  dkarcade title and its reveal follow. The plateau was at absolute frames ~270-297
  in these runs (depends on the exact start).
```

### Reproducible deterministic capture (recreate as needed)

The scratchpad scripts used this session are session-temporary. The essential
Python client (deterministic single frame; extend with a get-frame loop +
`wait-frame 1 90000` between frames for a sweep):

```python
import socket, hashlib, struct
def recv_line(f):
    l=b""
    while not l.endswith(b"\n"): l+=f.recv(1)
    return l.decode().rstrip("\n")
def recv_exact(f,n):
    b=b""
    while len(b)<n: b+=f.recv(n-len(b))
    return b
s=socket.create_connection(("127.0.0.1",PORT),timeout=120); s.settimeout(120)
i=[0]
def cmd(t):
    i[0]+=1; s.sendall(f"{i[0]} {t}\n".encode()); line=recv_line(s); p=line.split()
    if len(p)>=2 and p[1]=="data":
        pay=recv_exact(s,int(p[3])); recv_exact(s,1); return line,pay
    return line,None
cmd("run"); cmd("wait-frame 320 90000")
line,pay=cmd("get-frame format=argb8888")            # 384*263*4 bytes ARGB8888
print(line, hashlib.sha256(pay).hexdigest())
```

Lit-row metric (mirrors the reveal signal): unpack `struct.unpack("<%dI"%(len(pay)//4), pay)`,
treat `px[0]` as border, count rows y in [0,263) with any `px[y*384+x] != border`
for x in [24,344).

### Register tracing technique

A temporary `fprintf(stderr, ...)` in `vicii_write_register`, gated on
`v->timing.frame_number == <frame>` and `reg in {0x11,0x16,0x18,...}`, then run
headless and read stderr, is the fastest way to see exactly what a title writes
per raster/cycle. Remember to remove it before building for real. This is how the
YSCROLL-constant finding was made.

## Status of the tree

```text
- Phases 1-5 committed; working tree clean at the point this doc was written.
- Full suite: 40/40 green.
- No revert recommended: the Phase 2-5 accuracy work is sound and independent of
  the reveal outcome.
```
