# VIC-II Phase B — Pixel-Accurate Smooth Scroll & Display Window Clamping

## Purpose

This document is a coding-agent-ready implementation guide for Phase B of the VIC-II
emulation in c64m. It covers pixel-accurate XSCROLL/YSCROLL, 24/25-row and 38/40-column
border clamping (RSEL/CSEL), and the border unit flip-flop model. Phase A (raster timing
and Bad Lines) must be complete before starting this phase.

## Reference

Christian Bauer, "The MOS 6567/6569 video controller (VIC-II) and its application in
the Commodore 64" (1996), sections 3.7 (scroll registers), 3.9 (border unit), and the
register table for $D011/$D016.

---

## Background: What Exists Today

`src/machine/vicii.h` and `src/machine/vicii.c` contain the current VIC-II
implementation. Relevant current state:

- `vicii_make_frame_snapshot()` renders one complete frame in a single pass: it loops
  over every pixel, decides whether it is inside a hardcoded active area (`VICII_ACTIVE_X`
  = 32, `VICII_ACTIVE_Y` = 36, `VICII_ACTIVE_W` = 320, `VICII_ACTIVE_H` = 200), and
  draws either a border pixel or a character glyph pixel. There is no border flip-flop
  and no scroll offset applied.
- `XSCROLL` and `YSCROLL` (bits 2–0 of `$D016` and `$D011`) are stored in
  `v->registers[0x16]` and `v->registers[0x11]` but are not used during rendering.
- `RSEL` (`$D011` bit 3) and `CSEL` (`$D016` bit 3) are stored but not applied to the
  border geometry.
- The `c64_frame` pixel buffer is 384 × 272 ARGB8888, matching a full PAL visible area.
  **PAL is the canonical geometry for this codebase.** All tests use PAL.

The snapshot renderer is the only path that produces pixels. Phase B replaces the
hardcoded geometry in that renderer with a border-unit model and applies scroll offsets.

---

## Concepts

### VIC-II Display Window

The VIC-II composites pixels in this priority order (back to front):

1. Background color / character background pixels
2. Character foreground pixels
3. Border color (when the border unit is active)

The *display window* is the rectangular region where character/bitmap graphics are
visible. Outside the display window the border color dominates. The display window
boundaries are controlled by RSEL and CSEL.

### Border Unit Flip-Flops (Bauer §3.9)

The border unit maintains two independent 1-bit flip-flops:

- **Vertical flip-flop** (`vborder`): controls whether the top/bottom border is active.
  - Cleared (border OFF) when `raster_y` reaches the *top* compare value.
  - Set (border ON) when `raster_y` reaches the *bottom* compare value.
  - Compare values depend on `RSEL` (PAL values; NTSC values shown for reference only):
    - `RSEL=1` (25 rows): top = 51, bottom = 251 (PAL); top = 41, bottom = 241 (NTSC)
    - `RSEL=0` (24 rows): top = 55, bottom = 247 (PAL); top = 45, bottom = 237 (NTSC)
- **Horizontal flip-flop** (`hborder`): controls whether the left/right border is
  active within each raster line.
  - Set (border ON) when the pixel X counter reaches the *right* compare value.
  - Cleared (border OFF) when the pixel X counter reaches the *left* compare value,
    **and** `vborder` is currently clear.
  - Compare values depend on `CSEL`:
    - `CSEL=1` (40 columns): left = 24, right = 344
    - `CSEL=0` (38 columns): left = 31, right = 335

If **either** `vborder` or `hborder` is set, the output pixel is the border color
(`$D020`). Otherwise the pixel comes from the graphics data.

The `hborder` flip-flop carries its state from one raster line to the next (it does not
reset at the start of each line). Since the right compare (x=344 or x=335) fires before
the end of each 384-pixel line, `hborder` will always be `true` at the end of a line,
so it is naturally `true` at the start of the next line — no per-line reset is needed.

### Border Compare Values

| RSEL | PAL Top | PAL Bottom |
|------|---------|------------|
| 1    | 51      | 251        |
| 0    | 55      | 247        |

| CSEL | Left X | Right X |
|------|--------|---------|
| 1    | 24     | 344     |
| 0    | 31     | 335     |

*X values are in pixel units within the 384-wide frame (`C64_FRAME_WIDTH`).
Y values are raster line numbers within the 272-line frame (`C64_FRAME_HEIGHT`).*

### YSCROLL — Formula Approach (Bauer §3.7.2)

`YSCROLL` (bits 2–0 of `$D011`, power-on default 3) shifts the display window content
vertically without moving the border boundary. The effect: the glyph row rendered at the
first display-window line is glyph row `YSCROLL` of the first character row, rather than
glyph row 0.

For a pixel at display-window-relative Y position `sy` (0 = first line inside top
border), the glyph row within the character cell is:

```
row_in_cell = (sy + 8 - YSCROLL) & 7      /* Bauer §3.7.2 */
char_row    = (sy + 8 - YSCROLL) / 8      /* 0-based character row, may overflow to 25 */
```

This is the correct approach for `vicii_make_frame_snapshot()`, which is a whole-frame
CPU-side draw and does not model per-cycle pixel output. The `vc`/`rc` counters
accumulated during `vicii_step_cycle()` are left in end-of-frame state and must not be
used to drive this from-scratch render. Annotate the formula with a comment citing
Bauer §3.7.2 and noting that `vc`/`rc` integration is deferred to a future per-cycle
renderer.

### XSCROLL (Bauer §3.7.1)

`XSCROLL` (bits 2–0 of `$D016`, default 0) shifts the display window content
horizontally by 0–7 pixels without moving the border boundary.

For a pixel at display-window-relative X position `sx_raw = x - g.left`:

```
sx = sx_raw + XSCROLL    /* pixel into the scrolled content; may be >= 320 */
col = sx / 8             /* character column, 0-39 */
bit = 0x80u >> (sx & 7)  /* bit within the glyph byte */
```

When `sx >= 320` the content has scrolled off-screen; render background color.

---

## Implementation Steps

### Step 1 — Add Border Geometry Constants

**File:** `src/machine/vicii.c`, top-level `enum`

```c
/* PAL border compare values (pixel/line units within 384×272 frame) */
VICII_PAL_VBORDER_TOP_25    = 51,
VICII_PAL_VBORDER_BOTTOM_25 = 251,
VICII_PAL_VBORDER_TOP_24    = 55,
VICII_PAL_VBORDER_BOTTOM_24 = 247,

VICII_HBORDER_LEFT_40  = 24,
VICII_HBORDER_RIGHT_40 = 344,
VICII_HBORDER_LEFT_38  = 31,
VICII_HBORDER_RIGHT_38 = 335,
```

NTSC border constants are not added in this phase; PAL is the canonical geometry.

### Step 2 — Add a Border Geometry Helper

**File:** `src/machine/vicii.c`

```c
typedef struct vicii_border_geometry {
    uint32_t top;
    uint32_t bottom;
    uint32_t left;
    uint32_t right;
} vicii_border_geometry;

static vicii_border_geometry vicii_get_border_geometry(const vicii *v) {
    vicii_border_geometry g;
    bool rsel = (v->registers[0x11] & 0x08u) != 0;
    bool csel = (v->registers[0x16] & 0x08u) != 0;

    g.top    = rsel ? VICII_PAL_VBORDER_TOP_25    : VICII_PAL_VBORDER_TOP_24;
    g.bottom = rsel ? VICII_PAL_VBORDER_BOTTOM_25 : VICII_PAL_VBORDER_BOTTOM_24;
    g.left   = csel ? VICII_HBORDER_LEFT_40       : VICII_HBORDER_LEFT_38;
    g.right  = csel ? VICII_HBORDER_RIGHT_40      : VICII_HBORDER_RIGHT_38;
    return g;
}
```

### Step 3 — Rewrite `vicii_make_frame_snapshot()`

Replace the body of `vicii_make_frame_snapshot()` with the border-unit model.
`vborder` and `hborder` are **local variables** — do not add them to `struct vicii`.

**Full replacement algorithm:**

```c
bool vicii_make_frame_snapshot(vicii *v, const c64_bus_t *bus,
                               c64_frame *out_frame, uint64_t machine_cycle) {
    vicii_border_geometry g;
    bool     vborder, hborder;
    uint8_t  xscroll, yscroll;
    uint8_t  border_index, background_index;
    uint32_t border_color, background_color;
    uint16_t screen_base, char_base;
    uint32_t x, y;

    assert(v);
    assert(bus);
    assert(out_frame);

    g = vicii_get_border_geometry(v);

    xscroll          = v->registers[0x16] & 0x07u;
    yscroll          = v->registers[0x11] & 0x07u;
    border_index     = v->registers[VICII_REG_BORDER_COLOR]       & 0x0fu;
    background_index = v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu;
    border_color     = vicii_palette_argb[border_index];
    background_color = vicii_palette_argb[background_index];
    screen_base      = (uint16_t)((v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    char_base        = (uint16_t)(((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);

    v->working_frame.width        = C64_FRAME_WIDTH;
    v->working_frame.height       = C64_FRAME_HEIGHT;
    v->working_frame.stride_bytes = C64_FRAME_WIDTH * sizeof(v->working_frame.pixels[0]);
    v->working_frame.pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    v->working_frame.frame_number = v->timing.frame_number;
    v->working_frame.machine_cycle= machine_cycle;

    vborder = true;   /* border active until the top compare fires */
    hborder = true;

    for (y = 0; y < C64_FRAME_HEIGHT; y++) {

        /* Vertical flip-flop: evaluated at the start of each raster line */
        if (y == g.top)    vborder = false;
        if (y == g.bottom) vborder = true;

        for (x = 0; x < C64_FRAME_WIDTH; x++) {
            uint32_t pixel;

            /* Horizontal flip-flop transitions (Bauer §3.9) */
            if (x == g.right) hborder = true;
            if (x == g.left && !vborder) hborder = false;

            if (vborder || hborder) {
                pixel = border_color;
            } else {
                /* Display window: apply scroll and fetch graphics data */
                uint32_t sx_raw = x - g.left;
                uint32_t sy     = y - g.top;

                /* XSCROLL shifts content right; pixels past col 39 show background */
                uint32_t sx = sx_raw + xscroll;

                /* YSCROLL shifts content down (Bauer §3.7.2).
                   The vc/rc path is deferred to the future per-cycle renderer. */
                uint32_t row_in_cell = (sy + 8u - yscroll) & 7u;
                uint32_t char_row    = (sy + 8u - yscroll) / 8u;

                if (sx >= 320u || char_row >= 25u) {
                    pixel = background_color;
                } else {
                    uint32_t col  = sx / 8u;
                    uint8_t  bit  = (uint8_t)(0x80u >> (sx & 7u));
                    uint16_t cell = (uint16_t)(char_row * 40u + col);
                    uint8_t  code = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                    uint8_t  glyph= c64_bus_vic_read_char_glyph_at(bus, char_base,
                                                                     code,
                                                                     (uint8_t)row_in_cell);
                    uint8_t  fg   = c64_bus_vic_read_color(bus, cell);

                    pixel = (glyph & bit) ? vicii_palette_argb[fg & 0x0fu]
                                          : background_color;
                }
            }

            v->working_frame.pixels[y * C64_FRAME_WIDTH + x] = pixel;
        }
    }

    memcpy(out_frame, &v->working_frame, sizeof(*out_frame));
    return true;
}
```

**Remove** the four `VICII_ACTIVE_*` constants from the rendering loop. Grep for
`VICII_ACTIVE_X`, `VICII_ACTIVE_Y`, `VICII_ACTIVE_W`, `VICII_ACTIVE_H` across the
codebase before deleting them from `vicii.h` — they are referenced by the existing
tests (which the next step updates). Remove them from `vicii.h` once the tests no longer
reference them.

### Step 4 — Update Existing Tests for PAL + New Geometry

**File:** `tests/machine/test_c64_vicii.c`

#### 4a — Canonical PAL fixture

The `reset_machine()` helper currently leaves the machine in NTSC mode (the default
after `c64_init()`). PAL is the canonical geometry for this codebase (the 384 × 272
frame is PAL-sized). Update `reset_machine()` to set PAL mode after init:

```c
static void reset_machine(c64_t *machine) {
    c64_rom_set roms;
    c64_config  cfg;
    char error[256];

    build_roms(&roms);
    c64_init(machine);

    /* PAL is the canonical video standard for all tests: the 384×272 pixel
       buffer matches PAL dimensions and border compare values. */
    cfg.video_standard = C64_VIDEO_STANDARD_PAL;
    c64_set_config(machine, &cfg);

    expect_true("install synthetic ROMs", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}
```

#### 4b — Fix `test_frame_snapshot_geometry_and_regions()`

The test picks a pixel "inside the active area" using the old `VICII_ACTIVE_*` offsets.
Replace with the PAL, CSEL=1/RSEL=1 geometry (left=24, top=51):

```c
static void test_frame_snapshot_geometry_and_regions(void) {
    c64_t machine;
    c64_frame frame;
    uint32_t corner;
    uint32_t active;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x05);

    expect_true("make frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("frame width",  C64_FRAME_WIDTH,  frame.width);
    expect_u32("frame height", C64_FRAME_HEIGHT, frame.height);
    expect_u32("frame stride", C64_FRAME_WIDTH * sizeof(frame.pixels[0]), frame.stride_bytes);
    expect_u32("frame format", C64_FRAME_PIXEL_FORMAT_ARGB8888, frame.pixel_format);
    expect_u64("frame number", 0, frame.frame_number);
    expect_u64("frame cycle",  0, frame.machine_cycle);

    corner = frame.pixels[0];
    /* PAL CSEL=1/RSEL=1: display window starts at x=24, y=51 */
    active = frame.pixels[(51 + 10) * C64_FRAME_WIDTH + (24 + 10)];
    expect_true("corner is visible border", corner != 0);
    expect_not_u32("active differs from border", corner, active);
}
```

#### 4c — Fix `test_character_rendering_uses_screen_char_rom_and_color_ram()`

With YSCROLL=3 (power-on default), the renderer shows glyph row 5 at `sy=0`, but the
synthetic character ROM only has data in rows 0–3 — so the foreground pixel would not
appear at `sy=0`. Force `YSCROLL=0` by writing `$D011 = 0x1B` (DEN=1, RSEL=1,
YSCROLL=0) so glyph row 0 appears at the first display-window line. Update the pixel
coordinates to use the PAL geometry:

```c
static void test_character_rendering_uses_screen_char_rom_and_color_ram(void) {
    c64_t machine;
    c64_frame frame_a;
    c64_frame frame_b;
    uint32_t glyph_pixel;
    uint32_t background_pixel;
    uint32_t next_row_glyph_pixel;

    reset_machine(&machine);

    c64_bus_write(&machine.bus, 0xd021, 0x06);
    /* Force YSCROLL=0 so glyph row 0 appears at the first display-window line.
       0x1B = DEN=1, RSEL=1, YSCROLL=0. Without this, the default YSCROLL=3 would
       place glyph row 5 at sy=0, and the synthetic ROM has no data in row 5. */
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;

    expect_u8("character rom glyph fetch", 0x80, c64_bus_vic_read_char_glyph(&machine.bus, 1, 0));
    expect_u8("screen ram fetch",  1, c64_bus_vic_read_screen(&machine.bus, 0));
    expect_u8("color ram fetch",   5, c64_bus_vic_read_color(&machine.bus, 0));

    expect_true("make glyph frame",        c64_make_frame_snapshot(&machine, &frame_a));
    expect_true("make second glyph frame", c64_make_frame_snapshot(&machine, &frame_b));

    /* PAL CSEL=1/RSEL=1: display window starts at x=24, y=51.
       Character 1, glyph row 0 = 0x80: leftmost bit set → foreground at sx=0 (x=24). */
    glyph_pixel          = frame_a.pixels[51 * C64_FRAME_WIDTH + 24];
    background_pixel     = frame_a.pixels[51 * C64_FRAME_WIDTH + 25];
    next_row_glyph_pixel = frame_a.pixels[52 * C64_FRAME_WIDTH + 25];

    expect_u32("glyph foreground color",   TEST_COLOR_GREEN, glyph_pixel);
    expect_u32("glyph background color",   TEST_COLOR_BLUE,  background_pixel);
    expect_u32("second glyph row foreground", TEST_COLOR_GREEN, next_row_glyph_pixel);
    expect_u32("deterministic glyph pixel",
        frame_a.pixels[51 * C64_FRAME_WIDTH + 24],
        frame_b.pixels[51 * C64_FRAME_WIDTH + 24]);
}
```

Note: `next_row_glyph_pixel` checks `(y=52, x=25)`. With synthetic character 1, glyph
row 1 = 0x40 (bit 6 set), so sx=1 (x=25) should be foreground. Verify this matches the
synthetic ROM before running.

### Step 5 — Add Phase B Tests

Add the following three test functions to `tests/machine/test_c64_vicii.c` and call
them from `main()`.

#### `test_border_rsel_csel()`

Verifies RSEL=0 and CSEL=0 extend their respective borders.

```c
static void test_border_rsel_csel(void) {
    c64_t machine;
    c64_frame frame;
    uint32_t border_color;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red border */
    c64_bus_write(&machine.bus, 0xd021, 0x05); /* green background */

    /* RSEL=0: top border extends from y=51 to y=54 (4 extra lines).
       Write $D011 = 0x13: DEN=1, RSEL=0, YSCROLL=3. */
    c64_bus_write(&machine.bus, 0xd011, 0x13);
    expect_true("make frame rsel0", c64_make_frame_snapshot(&machine, &frame));
    border_color = vicii_palette_argb[2]; /* red */
    /* y=52 is inside the extended top border (RSEL=0 top=55, was 51) */
    expect_u32("rsel0 extended top border at y=52", border_color,
               frame.pixels[52 * C64_FRAME_WIDTH + 50]);
    /* y=56 is inside the display window with RSEL=0 (top=55, so y=55 clears vborder) */
    expect_not_u32("rsel0 display at y=56", border_color,
                   frame.pixels[56 * C64_FRAME_WIDTH + 50]);

    /* CSEL=0: left border extends from x=24 to x=30 (7 extra pixels).
       Write $D016 = 0x08: CSEL=0, XSCROLL=0. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x05);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    expect_true("make frame csel0", c64_make_frame_snapshot(&machine, &frame));
    /* x=26 is inside the extended left border (CSEL=0 left=31, was 24) */
    expect_u32("csel0 extended left border at x=26", border_color,
               frame.pixels[60 * C64_FRAME_WIDTH + 26]);
    /* x=32 is inside the display window with CSEL=0 */
    expect_not_u32("csel0 display at x=32", border_color,
                   frame.pixels[60 * C64_FRAME_WIDTH + 32]);
}
```

#### `test_xscroll_shifts_content()`

Verifies that incrementing XSCROLL shifts foreground pixels one pixel to the right.

```c
static void test_xscroll_shifts_content(void) {
    c64_t machine;
    c64_frame frame0, frame1;

    reset_machine(&machine);
    /* Force YSCROLL=0 and CSEL=1. Character 1 glyph row 0 = 0x80: bit at sx=0. */
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;

    expect_true("xscroll0 frame", c64_make_frame_snapshot(&machine, &frame0));

    c64_bus_write(&machine.bus, 0xd016, 0x19); /* XSCROLL=1 */
    expect_true("xscroll1 frame", c64_make_frame_snapshot(&machine, &frame1));

    /* With XSCROLL=0: foreground at x=24 (left=24, sx=0, glyph bit 7 set) */
    expect_u32("xscroll0 fg at x=24", vicii_palette_argb[5],
               frame0.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("xscroll0 bg at x=25", vicii_palette_argb[6],
               frame0.pixels[51 * C64_FRAME_WIDTH + 25]);

    /* With XSCROLL=1: the same glyph bit 7 now lands at sx=0 → x=25.
       x=24 shows bit 7 of the previous column (none, so background). */
    expect_u32("xscroll1 bg at x=24", vicii_palette_argb[6],
               frame1.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("xscroll1 fg at x=25", vicii_palette_argb[5],
               frame1.pixels[51 * C64_FRAME_WIDTH + 25]);
}
```

#### `test_yscroll_shifts_content()`

Verifies that incrementing YSCROLL shifts content one raster line down.

```c
static void test_yscroll_shifts_content(void) {
    c64_t machine;
    c64_frame frame0, frame1;

    reset_machine(&machine);
    /* YSCROLL=0: glyph row 0 at sy=0 (y=51). Character 1 glyph row 0 = 0x80. */
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;

    expect_true("yscroll0 frame", c64_make_frame_snapshot(&machine, &frame0));

    c64_bus_write(&machine.bus, 0xd011, 0x1c); /* YSCROLL=1 */
    expect_true("yscroll1 frame", c64_make_frame_snapshot(&machine, &frame1));

    /* YSCROLL=0: glyph row 0 at y=51 → foreground at (y=51, x=24) */
    expect_u32("yscroll0 fg at y=51", vicii_palette_argb[5],
               frame0.pixels[51 * C64_FRAME_WIDTH + 24]);

    /* YSCROLL=1: row_in_cell at sy=0 = (0+8-1)&7 = 7 → glyph row 7 = 0x00 (no fg).
       Glyph row 0 now appears at sy=1 (y=52). */
    expect_u32("yscroll1 bg at y=51", vicii_palette_argb[6],
               frame1.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("yscroll1 fg at y=52", vicii_palette_argb[5],
               frame1.pixels[52 * C64_FRAME_WIDTH + 24]);
}
```

Call all three from `main()`:

```c
test_border_rsel_csel();
test_xscroll_shifts_content();
test_yscroll_shifts_content();
```

---

## Register Reference

| Register | Address | Relevant Bits | Purpose |
|----------|---------|---------------|---------|
| `$D011` (CONTROL_1) | 0x11 | bit 4 = DEN | Display enable |
| `$D011` (CONTROL_1) | 0x11 | bit 3 = RSEL | 1=25 rows, 0=24 rows |
| `$D011` (CONTROL_1) | 0x11 | bits 2–0 = YSCROLL | Vertical scroll 0–7 |
| `$D016` (CONTROL_2) | 0x16 | bit 3 = CSEL | 1=40 columns, 0=38 columns |
| `$D016` (CONTROL_2) | 0x16 | bits 2–0 = XSCROLL | Horizontal scroll 0–7 |
| `$D020` (border) | 0x20 | bits 3–0 | Border color index |
| `$D021` (background 0) | 0x21 | bits 3–0 | Background color index |

All register reads and writes are already handled by `vicii_read_register()` and
`vicii_write_register()`. No changes to the register dispatch are needed for this phase.

---

## Files to Modify

| File | Change |
|------|--------|
| `src/machine/vicii.c` | Add border geometry constants and `vicii_get_border_geometry()`; rewrite `vicii_make_frame_snapshot()` |
| `src/machine/vicii.h` | Remove `VICII_ACTIVE_*` constants once tests no longer reference them |
| `tests/machine/test_c64_vicii.c` | Set PAL in `reset_machine()`; update two existing tests; add three new tests |

No new files are required. No changes to `struct vicii` fields.

---

## Acceptance Criteria

1. Default register state (`YSCROLL=3`, `XSCROLL=0`, `RSEL=1`, `CSEL=1`) produces
   a 25-row, 40-column display window starting at raster line 51 and pixel X=24 (PAL).
2. Setting `RSEL=0` moves the top border compare to line 55, extending the top and
   bottom borders by 4 lines each. The display window shrinks to 24 rows.
3. Setting `CSEL=0` moves the left border compare to X=31, extending the left and
   right borders by 7 pixels each. The display window shrinks to 38 columns.
4. Incrementing `XSCROLL` by 1 shifts all character pixel data 1 pixel to the right
   within the display window; the border boundary does not move.
5. Incrementing `YSCROLL` by 1 shifts all character pixel data 1 raster line down
   within the display window; the border boundary does not move.
6. `YSCROLL=3` (power-on default) with a character whose data starts at glyph row 0
   shows glyph row 3 at `sy=0` and glyph row 0 at `sy=5`.
7. All existing VIC-II tests continue to pass after coordinate and PAL-mode updates.
8. The three new Phase B tests (`test_border_rsel_csel`, `test_xscroll_shifts_content`,
   `test_yscroll_shifts_content`) all pass.
