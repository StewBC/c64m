# C64MVICII_SIDEBORDER — Horizontal (side) border opening

Status: planning. No code written yet.
Author of plan: research pass over `src/machine/vicii.c`, `md-files/lft-nine.md`,
`docs/status/VICII.md`.

## 1. Objective

Give the live VIC-II renderer a real **main (horizontal) border flip-flop** so a
timed `$D016` CSEL write can open the left/right side borders and reveal sprites
and idle/ghost-byte graphics there, exactly as `samples/lft-nine.prg` requires.

Today the side border is a fixed geometric mask (`x < g->left || x >= g->right`)
computed from the *current* CSEL. There is no horizontal border **state**, so a
mid-line CSEL change cannot leave the border open for the rest of the line.

Scope for this phase:

- PAL (6569) first. NTSC (6567R8) uses the same flip-flop and the same visible
  compare mapping; only `cycles_per_line` differs. NTSC gets the same code path
  and one regression test, but PAL `lft-nine` is the acceptance target.
- Left and right side-border opening via the main border flip-flop.
- Idle/ghost-byte graphics rendered in the *opened* side-border region (not just
  the border colour), so "ghost byte shine-through" is visible.

Out of scope for this phase (keep deferred):

- Sub-dot / half-cycle AEC-boundary write acceptance (`lft-nine.md` item 6).
- The late top-border sprite-multiplex timing failure (`lft-nine.md` item 5) —
  that is a separate CPU/BA budget bug, not a border-unit bug.

## 2. Coordinate model — the timings a coder needs

This is the part that must be settled before coding. Everything below is PAL
6569 unless noted.

### 2.1 One coordinate: buffer-x == VIC X-coordinate

The live renderer already treats the 384-wide frame buffer column as the VIC
**X-coordinate** (the same 9-bit space used for sprite X and the border
compares):

- background display spans buffer columns `24..343` (`sx_raw = x - 24`, 40 chars);
- sprites are drawn at `buffer_x == sprite_line_x` (`vicii_sprite_dx_wrapped`);
- the border compare constants are literally X-coordinates:
  `LEFT_40=24 ($18)`, `RIGHT_40=344 ($158)`, `LEFT_38=31 ($1f)`, `RIGHT_38=335 ($14f)`.

So the 384 buffer is a crop of the PAL line showing VIC-X `0..383`: 24px left
border, 320px display, 40px right border. VIC-X `384..503` (far border + H-blank
+ retrace) is off-buffer and not needed for visible side borders.

**Invariant to preserve:** `buffer_x` is the VIC X-coordinate. The border
flip-flop compares run in this same space.

### 2.2 Cycle ↔ X-coordinate anchor (derived, cross-checked)

`cycle_in_line` in `struct vicii_timing` is **0-based** (`0..62` PAL). Anchor it
to the standard (Bauer, 1-based) cycle numbering via the sprite p-access table
already in the code:

```
vicii_pal_sprite_fetch_cycle = {57,59,61,0,2,4,6,8}   // sprites 0..7, 0-based
```

Sprite 0 p-access at 0-based cycle 57 == standard 6569 cycle 58 (1-based), and
sprites 3..7 at 0-based {0,2,4,6,8} == standard {1,3,5,7,9}. Therefore:

```
c64m_cycle (0-based)  =  Bauer_cycle (1-based) - 1
```

The 40 c-accesses run at c64m cycles `15..54` (`VICII_CACCESS_FIRST_CYCLE=15`),
column `col = cycle - 15`, and display column 0 is drawn at the left-40 edge
X=24. Anchoring the first display dot to the first c-access cycle gives the
mapping used for **both** the border compares and the paint order:

```
X_first_dot(cycle) = 24 + (cycle - 15) * 8          // c64m 0-based cycle
cycle_of_X(X)      = 15 + (X - 24) / 8              // first dot of that cycle
```

Each cycle paints 8 dots: `[X_first_dot(cycle), X_first_dot(cycle)+8)`.

### 2.3 Where each compare lands (PAL)

| Compare              | X   | c64m cycle (0-based) | dot within cycle |
|----------------------|-----|----------------------|------------------|
| LEFT_40  (CSEL=1)    | 24  | 15                   | first dot        |
| LEFT_38  (CSEL=0)    | 31  | 15                   | last dot         |
| RIGHT_38 (CSEL=0)    | 335 | 53                   | last dot         |
| RIGHT_40 (CSEL=1)    | 344 | 55                   | first dot        |

Buffer-column coverage under this mapping:

- cols `0..23`   painted at cycles `12,13,14`  (left border);
- cols `24..343` painted at cycles `15..54`    (display);
- cols `344..383` painted at cycles `55..59`   (right border);
- cycles `0..11` and `60..62` paint X off-buffer (H-blank/far border) — clipped.

### 2.4 The side-border-open write window (PAL)

To keep the **right** border open, the main FF must never be set at the right
compare for this line:

- while the beam is at X=335 (end of cycle 53) CSEL must be **1** (right=344, no
  match);
- before the beam reaches X=344 (start of cycle 55) CSEL must be flipped to
  **0** (right=335, already passed, no match).

=> the `$D016` CSEL 1→0 write must be applied at **c64m cycle 54** (Bauer cycle
55). This matches the well-known "cycle 55" PAL side-border trick, which is the
cross-check that the anchor above is right.

The **left** re-open on the following line is governed by rule 6 (reset at left
compare only if the vertical FF is clear) with the CSEL live at cycle 15. The
exact left toggle cycle the demo uses is confirmed against VICE in step 6; the
flip-flop model reproduces whatever CSEL sequence the demo writes.

### 2.5 NTSC note

NTSC 6567R8 is 65 cycles/520 dots. The c-access window and the visible compare
X-values are identical; the display still occupies buffer `24..343`. So
`X_first_dot`/`cycle_of_X` above are unchanged for the visible region; only the
off-buffer tail is longer. No NTSC-specific compare constants are needed.

## 3. Border flip-flop model

Model the two hardware flip-flops (Bauer 3.9), reusing the existing vertical
state:

- `vertical_border_active` — already implemented (`vicii_update_live_vertical_border`,
  set/cleared at the raster-line top/bottom compares). Keep as-is.
- `main_border_ff` — new `bool` in `struct vicii`.

Per **dot** (buffer column), evaluated in increasing X as the line is painted:

1. if `x == right(current CSEL)` → `main_border_ff = true`   (rule 1)
2. if `x == left(current CSEL)` and `!vertical_border_active` → `main_border_ff = false` (rule 6)

`current CSEL` = live `registers[0x16] & 0x08` at the cycle painting that column.
`left/right(CSEL)` = existing `vicii_get_border_geometry` values.

Pixel is border when `main_border_ff || vertical_border_active`. This replaces
the geometric `hborder = x < left || x >= right` in `vicii_live_pixel` and the
`any_sprite_enabled` fast path.

The FF is **stateful across cycles and lines** — it is a struct field, only
mutated at the two compare columns. It naturally carries the right-border-set
state through the off-buffer tail and into the next line until the left compare
resets it. Initialise `main_border_ff = true` in `vicii_reset` (mirror the
`vertical_border_active = true` line); it self-corrects within the first line.

Rules 2–5 (vertical FF set/reset at the left compare combined with top/bottom)
are already approximated by the per-line `vicii_update_live_vertical_border`.
Do **not** re-derive vertical state here; only rules 1 and 6 for the horizontal
FF. This is the standard simplified model and is sufficient for `lft-nine`.

## 4. Implementation steps

### Step A — paint order must match the X mapping (prerequisite)

`vicii_render_live_cycle` currently paints with a scaled mapping:

```c
x0 = cycle * C64_FRAME_WIDTH / cycles_per_line;   // ~6.1 px/cycle, NOT anchored
```

This smears the CSEL write window across ~1.5 buffer columns per cycle and does
**not** put X=335 before the write and X=344 after it, so the trick cannot work.
Replace it with the anchored mapping from §2.2:

```c
int32_t xs = 24 + ((int32_t)cycle - 15) * 8;   // first dot of this cycle
x0 = clamp(xs,          0, C64_FRAME_WIDTH);
x1 = clamp(xs + 8,      0, C64_FRAME_WIDTH);
```

Now each column is painted at its true VIC cycle, in increasing X, so the FF can
be advanced per column with the live CSEL. This is the "documented exact mapping
from VIC dots to the 384-px buffer" that `lft-nine.md` item 2 asks for.

**Regression surface (must re-baseline, see §5):** this changes *which cycle*
paints each column, so any mid-line register write is now sampled at the
physically-correct column instead of the scaled one. Content for a static screen
is unchanged (every column still painted exactly once with its own value).

### Step B — main border flip-flop state

- add `bool main_border_ff;` to `struct vicii` (next to `vertical_border_active`);
- init in `vicii_reset` to `true`;
- (optional) reset at start-of-line is **not** wanted — the FF must persist.

### Step C — evaluate the FF in the paint loop

In `vicii_render_live_cycle` / `vicii_live_pixel`, walk columns `x0..x1` in
order and for each `x`:

- compute `g = vicii_get_border_geometry(v)` once per cycle (CSEL is constant
  within a cycle);
- apply rules 1 and 2 to `v->main_border_ff`;
- pass `v->main_border_ff || v->vertical_border_active` as `border_active` to the
  sprite/background composition (replacing the geometric `hborder`).

Keep `vicii_get_border_geometry` (still the source of the compare X-values). The
snapshot/debug renderer (`vicii_render_snapshot`, line ~1631) keeps its geometric
side border — snapshots have no cycle history to run a flip-flop. Only the live
path changes.

### Step D — idle / ghost-byte graphics in the opened side border

Opening the border must reveal content, not the border colour. Two sources:

- **Sprites** already compose regardless of geometry once `border_active` is
  false — no change needed beyond Step C.
- **Background** currently short-circuits the side border to `b0c`:
  `vicii_background_pixel` returns early for `x < g->left` (line ~638) and for
  `col >= 40` (line ~688). For an open border on a display line we want the
  idle-state pixel (the ghost byte, `$3FFF`/`$39FF` in ECM) instead.

Change: when the column is outside the 40-column display span but the main FF is
**open** there, return `vicii_idle_pixel(v, bus, x)` rather than `b0c`. Note
`vicii_idle_pixel` computes `sx_raw = x - 24`; for `x < 24` that underflows —
extend it to handle the left over-border (use signed offset, or guard). The demo
only needs the ghost-byte pattern the existing idle path already produces; verify
ECM ghost address `$39FF` is used (it is, `vicii_fetch_g_or_idle_access`
line ~1148, and `vicii_idle_pixel` honours the ECM bit).

### Step E — `$D016` write path

No special handling is required in `vicii_write_register`: CSEL is read live from
`registers[0x16]` by the FF each cycle, and register writes are already applied
at cycle boundaries by the runtime before `vicii_step_cycle`. Confirm there is no
caching of CSEL/geometry that would defeat a mid-line change (there is not today
— `vicii_get_border_geometry` reads registers live). Leave `$D016` as an ordinary
register write.

## 5. Tests to update (regression) and add

### 5.1 Must re-baseline after Step A

These encode the *old* scaled cycle→column mapping and will need new expected
columns/values (recompute with `cycle_of_X`):

- `test_expose_harness_midline_injection_hits_exact_column`
  (`tests/machine/test_c64_vicii.c:1964`) — comment literally says
  "x=90 maps to ~cycle 15 ... x=140 to ~cycle 23" using `384/65`. Under the
  anchored mapping x=90 → cycle 23, x=140 → cycle 29 (both *after* the cycle-20
  write). Update expected pixels/columns accordingly.
- Re-run and re-baseline as needed: `test_expose_*` family
  (`:1938..:2154`), `test_live_bottom_border_can_be_opened_for_sprites`,
  `test_ntsc_live_bottom_border_can_be_opened_for_sprites`,
  `test_live_deep_bottom_border_sprite_is_painted`. Bottom/vertical tests should
  be unaffected in Y, but their X sample columns may shift.

Run the full `test_c64_vicii` binary and the whole `ctest` suite; treat any
change from Step A as expected re-timing, not a silent regression — verify each
delta is the physically-correct column before updating the expectation.

### 5.2 New unit tests (PAL, using `run_vic_frame_with_injections`)

1. **Right border opens.** Full-white bitmap or solid border. On one display
   raster inject `$D016` CSEL 1→0 at cycle 54; assert buffer columns `344..360`
   on that line are **not** border colour (show idle/sprite content), while the
   line above (no write) is border colour at the same columns.
2. **Right border stays closed without the trick.** Same setup, write at the
   wrong cycle (e.g. cycle 52, so X=335 compare sees CSEL=0 and sets the FF);
   assert columns `> 335` are border.
3. **Left border opens / re-closes across the line boundary.** Assert the FF
   state persists from one line's right-set into the next line and is reset at
   the left compare (columns `0..23` border, `24+` content) under normal CSEL.
4. **Sprite visible in an opened side border.** Place a sprite at X≈350 (right
   border), open the right border, assert the sprite colour appears at its
   columns.
5. **NTSC parity.** Repeat test 1 under `VICII_STANDARD_NTSC`; same visible
   compare cycles, assert the same reveal.

## 6. Hardware-oracle validation (VICE)

`lft-nine.md` item 3 requires a hardware oracle for the exact left toggle. Before
final acceptance:

- capture `samples/lft-nine.prg` under VICE x64sc PAL;
- for each per-line `$D016` write in the border-open kernel, record raster, VIC
  cycle, CSEL value, and first/last revealed dot;
- confirm the c64m first/last revealed buffer columns match VICE within the
  crop, and that the write cycles align with §2.4. Adjust only the mapping
  anchor constant if a measured dot offset remains — do **not** hand-tune per
  test.

## 7. Acceptance

- New PAL side-border unit tests pass; NTSC parity test passes.
- `samples/lft-nine.prg` PAL headless run shows revealed left/right columns with
  sprites/ghost-byte content instead of solid border, at settled frames
  (`build/c64m --headless --control-port <p> --pal -a -p samples/lft-nine.prg`;
  compare against the failing hash region in `lft-nine.md`).
- Full `ctest` passes (with the §5.1 expectations re-baselined). The two known
  unrelated failures (`c64_boot_progression`, `c64_robocop_g64`) are out of
  scope.
- `docs/status/VICII.md` updated: the "Horizontal border opening is not modeled"
  limitation moves from *deferred* to *implemented (live path)*; snapshot path
  still geometric. Update `docs/status/DEFERRED.md` and `docs/status/TESTING.md`.

## 8. Recommended commit sequence

1. Step A (anchored paint mapping) + re-baseline §5.1 tests. Isolated commit so
   the re-timing is reviewable on its own.
2. Steps B+C (main border flip-flop) + new tests 1–3.
3. Step D (idle/ghost-byte in opened border) + test 4.
4. NTSC parity test 5.
5. VICE validation + status-doc updates.

## 9. Open questions / risks

- **Step A blast radius.** The paint-order change is the riskiest piece. It is
  required (§2.4 proves the trick cannot work under the scaled mapping), but it
  re-times every mid-line write. Mitigation: land it alone, re-baseline, and
  eyeball each expectation delta against `cycle_of_X`.
- **Left-edge idle underflow.** `vicii_idle_pixel` assumes `x >= 24`. Extend for
  `x < 24` when the left border is open.
- **Exact left toggle cycle** is demo-specific and confirmed only in step 6; the
  FF model does not depend on knowing it in advance.
- **AEC-boundary write acceptance** (`lft-nine.md` item 6) stays deferred; only
  revisit if VICE shows a one-dot mismatch that tracks the CPU-write phase.
