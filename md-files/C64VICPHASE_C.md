# C64VICPHASE_C.md — VIC-II Phase C: Graphics Modes

## Purpose

This document is a coding-agent-ready implementation guide for VIC-II Phase C as
defined in `md-files/C64MVICII.md`. Read `md-files/AGENTS.md`, `md-files/MASTER.md`,
and `md-files/STATUS.md` before starting. Phases A and B must be complete. Implement
only what is described here.

Primary reference: Christian Bauer, "The MOS 6567/6569 video controller (VIC-II) and
its application in the Commodore 64" (1996), sections 3.3 (graphics modes), 3.4
(extended color mode), 3.5 (multicolor modes).

---

## Goal

Extend `vicii_make_frame_snapshot()` to support all eight ECM/BMM/MCM mode combinations.
Standard text mode (ECM=0, BMM=0, MCM=0) already works. This phase adds:

- MCM text mode (ECM=0, BMM=0, MCM=1)
- Standard bitmap mode (ECM=0, BMM=1, MCM=0)
- Multicolor bitmap mode (ECM=0, BMM=1, MCM=1)
- ECM text mode (ECM=1, BMM=0, MCM=0)
- Three invalid modes (ECM=1 with BMM or MCM): force all display pixels black

No changes to `vicii_step_cycle`, `struct vicii` fields, or `vicii.h`. All work is
in `vicii_make_frame_snapshot()` in `vicii.c` plus new tests in
`tests/machine/test_c64_vicii.c`.

---

## Background: What Exists Today

`vicii_make_frame_snapshot()` in `src/machine/vicii.c` renders one complete frame in a
single pass using the border flip-flop model from Phase B. For pixels inside the display
window it currently does one thing: standard text mode. The inner display block reads a
character code from screen RAM, fetches a glyph byte from character ROM, reads the
foreground color from color RAM, and outputs a 1-bit-per-pixel result against `b0c`
(background color 0).

The three mode bits are already stored in registers but ignored during rendering:
- `ECM`: `registers[0x11]` bit 6
- `BMM`: `registers[0x11]` bit 5
- `MCM`: `registers[0x16]` bit 4

Background color registers `$D022`–`$D024` (B1C, B2C, B3C) are stored in
`registers[0x22]`–`registers[0x24]` via the default register write path.

---

## Register Reference

| Address | Index  | Name      | Relevant bits for Phase C |
|---------|--------|-----------|---------------------------|
| `$D011` | `0x11` | CONTROL_1 | Bit 6 = ECM, bit 5 = BMM, bit 4 = DEN, bit 3 = RSEL, bits 2:0 = YSCROLL |
| `$D016` | `0x16` | CONTROL_2 | Bit 4 = MCM, bit 3 = CSEL, bits 2:0 = XSCROLL |
| `$D018` | `0x18` | MEM_PTR   | Bits 7:4 = screen base ÷ $400, bit 3 = bitmap base ($0000 or $2000) |
| `$D021` | `0x21` | BG_COLOR_0 | bits 3:0 = B0C |
| `$D022` | `0x22` | BG_COLOR_1 | bits 3:0 = B1C |
| `$D023` | `0x23` | BG_COLOR_2 | bits 3:0 = B2C |
| `$D024` | `0x24` | BG_COLOR_3 | bits 3:0 = B3C |

The mode, memory-pointer, and background-color registers needed by this phase are
already stored by the existing `vicii_write_register` path. No register dispatch
changes are needed for this phase.

---

## Mode Encoding

The three mode bits produce an 8-way dispatch. Encode as:

```
mode = (ECM << 2) | (BMM << 1) | MCM
```

| mode | ECM | BMM | MCM | Name               |
|------|-----|-----|-----|--------------------|
| 0    | 0   | 0   | 0   | Standard text      |
| 1    | 0   | 0   | 1   | MCM text           |
| 2    | 0   | 1   | 0   | Standard bitmap    |
| 3    | 0   | 1   | 1   | Multicolor bitmap  |
| 4    | 1   | 0   | 0   | ECM text           |
| 5    | 1   | 0   | 1   | Invalid (black)    |
| 6    | 1   | 1   | 0   | Invalid (black)    |
| 7    | 1   | 1   | 1   | Invalid (black)    |

---

## Per-Mode Pixel Rules (Bauer §3.3–3.5)

These rules apply to pixels inside the display window (non-border). Each rule applies
per character cell. The variables `col`, `cell`, `row_in_cell`, and `sx` are defined in
the implementation below.

### Mode 0 — Standard text (unchanged)

```
code  = screen_ram[screen_base + cell]
glyph = char_rom[char_base + code * 8 + row_in_cell]
fg    = color_ram[cell] & 0x0F
bit   = 0x80 >> (sx & 7)
pixel = (glyph & bit) ? palette[fg] : b0c
```

### Mode 1 — MCM text

Each character is rendered as either standard hires or multicolor, selected by bit 3
of its color RAM nibble (Bauer §3.3.2).

```
code      = screen_ram[screen_base + cell]
color_nib = color_ram[cell]

if (color_nib & 0x08) == 0:          /* hires character */
    glyph = char_rom[char_base + code * 8 + row_in_cell]
    bit   = 0x80 >> (sx & 7)
    pixel = (glyph & bit) ? palette[color_nib & 0x0F] : b0c

else:                                  /* multicolor character */
    glyph = char_rom[char_base + code * 8 + row_in_cell]
    pair  = (glyph >> (6 - (sx & 6))) & 3
    00 → b0c
    01 → b1c
    10 → b2c
    11 → palette[color_nib & 0x07]   /* lower 3 bits; bit 3 selects MCM, not color */
```

Two adjacent screen pixels share the same pair: pixels at `sx` and `sx|1` are
identical. This halves effective horizontal resolution to 4 pixel-pairs per character.

### Mode 2 — Standard bitmap

Color comes from the video matrix (screen RAM), not from color RAM (Bauer §3.3.3).
Bitmap data is fetched from `bitmap_base`, not from character ROM.

```
vm_byte   = screen_ram[screen_base + cell]
baddr     = bitmap_base + cell * 8 + row_in_cell
bitmap    = ram[baddr]
bit       = 0x80 >> (sx & 7)
fg_color  = palette[(vm_byte >> 4) & 0x0F]
bg_color  = palette[vm_byte & 0x0F]
pixel     = (bitmap & bit) ? fg_color : bg_color
```

`bitmap_base` is derived from bit 3 of `$D018`: `((registers[0x18] >> 3) & 1) * 0x2000`.

### Mode 3 — Multicolor bitmap

Pixel pairs; four colors per 8×8 block (Bauer §3.3.4).

```
vm_byte   = screen_ram[screen_base + cell]
color_nib = color_ram[cell]
baddr     = bitmap_base + cell * 8 + row_in_cell
bitmap    = ram[baddr]
pair      = (bitmap >> (6 - (sx & 6))) & 3
00 → b0c
01 → palette[(vm_byte >> 4) & 0x0F]   /* video matrix high nibble */
10 → palette[vm_byte & 0x0F]          /* video matrix low nibble */
11 → palette[color_nib & 0x0F]        /* color RAM nibble */
```

### Mode 4 — ECM text

Only 64 characters available; bits 6–7 of the character code select the background
color register (Bauer §3.4).

```
code    = screen_ram[screen_base + cell]
ecm_sel = (code >> 6) & 3          /* 0=B0C, 1=B1C, 2=B2C, 3=B3C */
glyph   = char_rom[char_base + (code & 0x3F) * 8 + row_in_cell]
fg_nib  = color_ram[cell] & 0x0F
bit     = 0x80 >> (sx & 7)
ecm_bg  = {b0c, b1c, b2c, b3c}[ecm_sel]
pixel   = (glyph & bit) ? palette[fg_nib] : ecm_bg
```

### Modes 5, 6, 7 — Invalid

All background/display-layer pixels are forced to black (`palette[0] = 0xff000000`);
the border color is unaffected. The snapshot renderer does not emulate invalid-mode
memory fetches because its reads have no timing or side effects. Per-cycle fetch
behavior remains the responsibility of the timing path.

---

## Implementation

### Step 1 — Add Enum Constants

In the `enum` block at the top of `src/machine/vicii.c`, add:

```c
/* Phase C: background color register indices */
VICII_REG_BACKGROUND_COLOR_1 = 0x22,
VICII_REG_BACKGROUND_COLOR_2 = 0x23,
VICII_REG_BACKGROUND_COLOR_3 = 0x24,
```

### Step 2 — Replace the Display Pixel Block in `vicii_make_frame_snapshot`

The existing function already handles border detection, geometry, xscroll, and yscroll.
The only block that changes is the display-pixel path inside the inner `for (x ...)` loop.

**Pre-loop additions** — add these variable declarations alongside the existing ones at
the top of `vicii_make_frame_snapshot`, after the existing setup of `xscroll`, `yscroll`,
`border_color`, `screen_base`, `char_base`:

```c
uint8_t  mode;
uint16_t bitmap_base;
uint32_t b0c, b1c, b2c, b3c;
```

**Pre-loop initialization** — add immediately after `char_base` is computed:

```c
mode        = (uint8_t)(((v->registers[0x11] & 0x40u) ? 4u : 0u) |
                        ((v->registers[0x11] & 0x20u) ? 2u : 0u) |
                        ((v->registers[0x16] & 0x10u) ? 1u : 0u));
bitmap_base = (uint16_t)(((v->registers[VICII_REG_MEMORY_POINTER] >> 3) & 1u) * 0x2000u);
b0c         = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu];
b1c         = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_1] & 0x0fu];
b2c         = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_2] & 0x0fu];
b3c         = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_3] & 0x0fu];
```

Also **remove** the existing `background_color` and `background_index` local variables
and replace every use of `background_color` inside the function with `b0c`.

**Replace the display pixel block** — the current `else` branch (non-border pixels)
is the standard-text path introduced in Phase B. It should already apply the delayed
YSCROLL top edge before computing `row_in_cell` and `char_row`:

```c
adjusted    = sy - (uint32_t)yscroll;
row_in_cell = adjusted & 7u;
char_row    = adjusted / 8u;
```

Replace the entire `else` branch body with:

```c
} else {
    uint32_t sx_raw = x - g.left;
    uint32_t sy     = y - g.top;

    if (mode >= 5u) {
        /* Invalid modes 5/6/7: background/display pixels forced black (Bauer §3.3) */
        pixel = vicii_palette_argb[0];
    } else if (sx_raw < (uint32_t)xscroll || sy < (uint32_t)yscroll) {
        /* Delayed scroll edges: background */
        pixel = b0c;
    } else {
        uint32_t sx          = sx_raw - (uint32_t)xscroll;
        uint32_t adjusted    = sy - (uint32_t)yscroll;
        uint32_t row_in_cell = adjusted & 7u;
        uint32_t char_row    = adjusted / 8u;
        uint32_t col         = sx / 8u;
        uint16_t cell        = (uint16_t)(char_row * 40u + col);

        switch (mode) {
        case 0u: /* Standard text */
            {
                uint8_t code  = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                uint8_t glyph = c64_bus_vic_read_char_glyph_at(bus, char_base, code,
                                                                 (uint8_t)row_in_cell);
                uint8_t fg    = c64_bus_vic_read_color(bus, cell);
                uint8_t bit   = (uint8_t)(0x80u >> (sx & 7u));
                pixel = (glyph & bit) ? vicii_palette_argb[fg & 0x0fu] : b0c;
            }
            break;

        case 1u: /* MCM text (Bauer §3.3.2) */
            {
                uint8_t code      = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                uint8_t color_nib = c64_bus_vic_read_color(bus, cell);
                uint8_t glyph     = c64_bus_vic_read_char_glyph_at(bus, char_base, code,
                                                                     (uint8_t)row_in_cell);
                if ((color_nib & 0x08u) == 0u) {
                    /* Standard hires character in MCM mode */
                    uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
                    pixel = (glyph & bit) ? vicii_palette_argb[color_nib & 0x0fu] : b0c;
                } else {
                    /* Multicolor character: pixel pairs, halved horizontal resolution.
                       Both pixels in a pair share the same color. sx&6 selects the pair. */
                    uint8_t pair = (uint8_t)((glyph >> (6u - (sx & 6u))) & 3u);
                    switch (pair) {
                    case 0u:  pixel = b0c; break;
                    case 1u:  pixel = b1c; break;
                    case 2u:  pixel = b2c; break;
                    default:  pixel = vicii_palette_argb[color_nib & 0x07u]; break;
                    }
                }
            }
            break;

        case 2u: /* Standard bitmap (Bauer §3.3.3) */
            {
                uint8_t  vm_byte = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                uint16_t baddr   = (uint16_t)(bitmap_base + (uint32_t)cell * 8u + row_in_cell);
                uint8_t  bdata   = c64_bus_vic_read_ram(bus, baddr);
                uint8_t  bit     = (uint8_t)(0x80u >> (sx & 7u));
                pixel = (bdata & bit) ? vicii_palette_argb[(vm_byte >> 4) & 0x0fu]
                                      : vicii_palette_argb[vm_byte & 0x0fu];
            }
            break;

        case 3u: /* Multicolor bitmap (Bauer §3.3.4) */
            {
                uint8_t  vm_byte   = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                uint8_t  color_nib = c64_bus_vic_read_color(bus, cell);
                uint16_t baddr     = (uint16_t)(bitmap_base + (uint32_t)cell * 8u + row_in_cell);
                uint8_t  bdata     = c64_bus_vic_read_ram(bus, baddr);
                uint8_t  pair      = (uint8_t)((bdata >> (6u - (sx & 6u))) & 3u);
                switch (pair) {
                case 0u:  pixel = b0c; break;
                case 1u:  pixel = vicii_palette_argb[(vm_byte >> 4) & 0x0fu]; break;
                case 2u:  pixel = vicii_palette_argb[vm_byte & 0x0fu]; break;
                default:  pixel = vicii_palette_argb[color_nib & 0x0fu]; break;
                }
            }
            break;

        case 4u: /* ECM text (Bauer §3.4) */
            {
                uint8_t  code    = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                uint8_t  ecm_sel = (code >> 6) & 3u;
                uint8_t  glyph   = c64_bus_vic_read_char_glyph_at(bus, char_base,
                                                                    code & 0x3fu,
                                                                    (uint8_t)row_in_cell);
                uint8_t  fg_nib  = c64_bus_vic_read_color(bus, cell);
                uint8_t  bit     = (uint8_t)(0x80u >> (sx & 7u));
                uint32_t ecm_bg;
                switch (ecm_sel) {
                case 0u:  ecm_bg = b0c; break;
                case 1u:  ecm_bg = b1c; break;
                case 2u:  ecm_bg = b2c; break;
                default:  ecm_bg = b3c; break;
                }
                pixel = (glyph & bit) ? vicii_palette_argb[fg_nib & 0x0fu] : ecm_bg;
            }
            break;

        default: /* unreachable: modes 5-7 handled above */
            pixel = vicii_palette_argb[0];
            break;
        }
    }
}
```

---

## Tests

All new tests go in `tests/machine/test_c64_vicii.c`. Add each function and call it
from `main()` after the existing Phase B tests.

### Test fixture notes

- `reset_machine()` sets PAL, installs synthetic ROMs, resets. Existing. Do not change.
- Each Phase C test must explicitly write `$D018 = 0x18` before writing display RAM.
  This sets `screen_base = $0400`, `char_base = $0000`, and `bitmap_base = $2000`.
  Do not rely on reset or KERNAL side effects for memory-pointer state.
- The synthetic character ROM has: `char[1*8+0] = 0x80`, `char[1*8+1] = 0x40`,
  `char[1*8+2] = 0x20`, `char[1*8+3] = 0x10`. All other rows of char 1 are 0x00.
- PAL display window with RSEL=1/CSEL=1: top-left at `(y=51, x=24)`.
- `$D011 = 0x18`: DEN=1, RSEL=1, ECM=0, BMM=0, YSCROLL=0. Sets display window left
  edge at x=24, glyph row 0 appears at sy=0 (y=51).
- `$D016 = 0x08`: CSEL=1, MCM=0, XSCROLL=0.
- `$D016 = 0x18`: CSEL=1, MCM=1, XSCROLL=0.

### Palette values used in assertions

```c
/* Reference: vicii_palette_argb in vicii.c */
#define TEST_PALETTE_0   0xff000000u  /* black */
#define TEST_PALETTE_5   0xff56ac4du  /* green  (also TEST_COLOR_GREEN) */
#define TEST_PALETTE_6   0xff2e2c9bu  /* blue   (also TEST_COLOR_BLUE)  */
#define TEST_PALETTE_10  0xffc46c71u  /* light red */
#define TEST_PALETTE_11  0xff4a4a4au  /* dark gray */
```

Add these defines to the top of `test_c64_vicii.c`, below the existing `TEST_COLOR_*`
defines.

### `test_ecm_text_mode()`

Verifies that ECM mode selects per-character background colors via bits 6–7 of the
character code.

Setup:
- `$D011 = 0x58`: ECM=1, BMM=0, DEN=1, RSEL=1, YSCROLL=0. (0x58 = 0b01011000)
- `$D016 = 0x08`: CSEL=1, MCM=0, XSCROLL=0.
- `$D021 = 0x06` (B0C = blue), `$D022 = 0x05` (B1C = green), `$D024 = 0x03` (B3C = cyan).
- `machine.bus.ram[0x0400] = 0x01`: cell 0 → code 0x01 (ecm_sel=0, char index 1).
- `machine.bus.ram[0x0401] = 0xC1`: cell 1 → code 0xC1 (ecm_sel=3, char index 1).
- `machine.bus.color_ram[0] = 0x05`: fg for cell 0.
- `machine.bus.color_ram[1] = 0x05`: fg for cell 1.

```c
static void test_ecm_text_mode(void) {
    c64_t    machine;
    c64_frame frame;
    uint32_t green, blue, cyan;

    green = TEST_PALETTE_5;
    blue  = TEST_PALETTE_6;
    cyan  = 0xff75cec8u; /* palette index 3 */

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, char=$0000 */
    /* ECM=1, DEN=1, RSEL=1, YSCROLL=0.  0x58 = 0101 1000 */
    c64_bus_write(&machine.bus, 0xd011, 0x58);
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */
    c64_bus_write(&machine.bus, 0xd022, 0x05); /* B1C = green */
    c64_bus_write(&machine.bus, 0xd024, 0x03); /* B3C = cyan */

    /* Cell 0: code=0x01 → ecm_sel=0 (B0C), char 1.  char[1*8+0]=0x80 → bit7 at sx=0. */
    machine.bus.ram[0x0400] = 0x01;
    machine.bus.color_ram[0] = 0x05; /* fg = green */

    /* Cell 1: code=0xC1 → ecm_sel=3 (B3C), char 1 (0xC1&0x3F=1).  Same glyph. */
    machine.bus.ram[0x0401] = 0xC1;
    machine.bus.color_ram[1] = 0x05; /* fg = green */

    expect_true("make ecm frame", c64_make_frame_snapshot(&machine, &frame));

    /* Cell 0, sx=0 (x=24): glyph bit7 set → fg (green) */
    expect_u32("ecm cell0 fg pixel", green, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* Cell 0, sx=1 (x=25): glyph bit6 clear → B0C (blue) */
    expect_u32("ecm cell0 bg is b0c", blue, frame.pixels[51 * C64_FRAME_WIDTH + 25]);

    /* Cell 1, sx=8 (x=32): glyph bit7 set → fg (green) */
    expect_u32("ecm cell1 fg pixel", green, frame.pixels[51 * C64_FRAME_WIDTH + 32]);
    /* Cell 1, sx=9 (x=33): glyph bit6 clear → B3C (cyan) */
    expect_u32("ecm cell1 bg is b3c", cyan, frame.pixels[51 * C64_FRAME_WIDTH + 33]);
}
```

### `test_standard_bitmap_mode()`

Verifies that bitmap mode reads foreground/background colors from the video matrix byte
and pixel data from the bitmap area.

Setup:
- `$D018 = 0x18`: screen_base = `$0400`, char_base = `$0000`,
  bitmap_base = `$2000`.
- `$D011 = 0x38`: BMM=1, DEN=1, RSEL=1, YSCROLL=0. (0x38 = 0011 1000)
- `$D016 = 0x08`: CSEL=1, MCM=0, XSCROLL=0.
- `machine.bus.ram[0x2000] = 0x80`: bitmap byte for cell 0, row 0. Bit 7 set → fg at sx=0.
- `machine.bus.ram[0x0400] = 0xAB`: vm_byte for cell 0. High nibble 0xA → fg color
  (palette[10]), low nibble 0xB → bg color (palette[11]).

```c
static void test_standard_bitmap_mode(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, bitmap=$2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x38); /* BMM=1, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */

    machine.bus.ram[0x2000] = 0x80; /* cell 0, row 0: bit 7 set */
    machine.bus.ram[0x0400] = 0xAB; /* vm_byte: fg=palette[10], bg=palette[11] */

    expect_true("make bitmap frame", c64_make_frame_snapshot(&machine, &frame));

    /* sx=0 (x=24): bitmap bit7=1 → fg = palette[10] */
    expect_u32("bitmap fg at x=24", TEST_PALETTE_10, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* sx=1 (x=25): bitmap bit6=0 → bg = palette[11] */
    expect_u32("bitmap bg at x=25", TEST_PALETTE_11, frame.pixels[51 * C64_FRAME_WIDTH + 25]);
}
```

### `test_multicolor_bitmap_mode()`

Verifies four-color pixel pairs in multicolor bitmap mode.

Bitmap byte `0xE4 = 1110 0100`:
- bits 7:6 = 11 → pair 3 → palette[color_ram[0]]
- bits 5:4 = 10 → pair 2 → palette[vm_byte & 0x0F]
- bits 3:2 = 01 → pair 1 → palette[(vm_byte >> 4) & 0x0F]
- bits 1:0 = 00 → pair 0 → B0C

```c
static void test_multicolor_bitmap_mode(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  blue;

    blue = TEST_PALETTE_6;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, bitmap=$2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x38); /* BMM=1, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */

    machine.bus.ram[0x2000] = 0xE4; /* pairs: (11)(10)(01)(00) at sx=0,2,4,6 */
    machine.bus.ram[0x0400] = 0xAB; /* vm_byte: high=0xA→palette[10], low=0xB→palette[11] */
    machine.bus.color_ram[0] = 0x05; /* pair 11 → palette[5] = green */

    expect_true("make mcm bitmap frame", c64_make_frame_snapshot(&machine, &frame));

    /* sx=0 (x=24): pair=3 → palette[color_ram[0]] = palette[5] = green */
    expect_u32("mcmbm pair11 at x=24", TEST_PALETTE_5, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* sx=2 (x=26): pair=2 → palette[vm_byte & 0x0F] = palette[0xB] = palette[11] */
    expect_u32("mcmbm pair10 at x=26", TEST_PALETTE_11, frame.pixels[51 * C64_FRAME_WIDTH + 26]);
    /* sx=4 (x=28): pair=1 → palette[(vm_byte>>4) & 0x0F] = palette[0xA] = palette[10] */
    expect_u32("mcmbm pair01 at x=28", TEST_PALETTE_10, frame.pixels[51 * C64_FRAME_WIDTH + 28]);
    /* sx=6 (x=30): pair=0 → B0C = blue */
    expect_u32("mcmbm pair00 at x=30", blue, frame.pixels[51 * C64_FRAME_WIDTH + 30]);
}
```

### `test_mcm_text_mode()`

Verifies both the hires-character path (color bit 3 clear) and the multicolor-character
path (color bit 3 set) within MCM text mode.

```c
static void test_mcm_text_mode(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  green, blue;

    green = TEST_PALETTE_5;
    blue  = TEST_PALETTE_6;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, char=$0000 */
    c64_bus_write(&machine.bus, 0xd011, 0x18); /* DEN=1, RSEL=1, ECM=0, BMM=0, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */
    c64_bus_write(&machine.bus, 0xd023, 0x05); /* B2C = green */

    /* Cell 0: char 1, color_nib=5 (bit3=0 → hires). Glyph row 0=0x80: bit7 at sx=0. */
    machine.bus.ram[0x0400] = 0x01;
    machine.bus.color_ram[0] = 0x05;

    /* Cell 1: char 1, color_nib=0x0D (bit3=1 → MCM, color bits=5).
       Glyph row 0=0x80=1000 0000 → pairs: (10)(00)(00)(00).
       sx=8 (col 1 start, x=32): sx&6=0, pair=(0x80>>6)&3=2 → B2C = green.
       sx=10 (x=34): sx&6=2, pair=(0x80>>4)&3=0 → B0C = blue. */
    machine.bus.ram[0x0401] = 0x01;
    machine.bus.color_ram[1] = 0x0D;

    expect_true("make mcm text frame", c64_make_frame_snapshot(&machine, &frame));

    /* Cell 0 hires: sx=0 (x=24) → glyph bit7=1 → fg = palette[5] = green */
    expect_u32("mcm hires fg at x=24", green, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* Cell 0 hires: sx=1 (x=25) → glyph bit6=0 → B0C = blue */
    expect_u32("mcm hires bg at x=25", blue, frame.pixels[51 * C64_FRAME_WIDTH + 25]);

    /* Cell 1 multicolor: sx=8 (x=32) → pair=2 → B2C = green */
    expect_u32("mcm color pair2 at x=32", green, frame.pixels[51 * C64_FRAME_WIDTH + 32]);
    /* Cell 1 multicolor: sx=10 (x=34) → pair=0 → B0C = blue */
    expect_u32("mcm color pair0 at x=34", blue, frame.pixels[51 * C64_FRAME_WIDTH + 34]);
}
```

### `test_invalid_mode_forces_black()`

Verifies that modes 5, 6, and 7 force all display pixels to black while leaving the
border unaffected.

```c
static void test_invalid_mode_forces_black(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  black, border_color;

    black = TEST_PALETTE_0; /* 0xff000000 */

    /* Mode 6: ECM=1, BMM=1 → $D011 bit6=1, bit5=1 */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x78); /* ECM=1, BMM=1, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* border = red (not black) */

    expect_true("make invalid frame", c64_make_frame_snapshot(&machine, &frame));

    border_color = frame.pixels[0]; /* top-left is always border */
    expect_not_u32("invalid border not black", black, border_color);

    /* Display window pixel: must be black */
    expect_u32("invalid display black at (51,24)", black,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("invalid display black at (100,100)", black,
               frame.pixels[100 * C64_FRAME_WIDTH + 100]);

    /* Mode 5: ECM=1, MCM=1 */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x58); /* ECM=1, BMM=0, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02);

    expect_true("make mode5 frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("mode5 display black", black, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
}
```

### Add to `main()`

```c
test_ecm_text_mode();
test_standard_bitmap_mode();
test_multicolor_bitmap_mode();
test_mcm_text_mode();
test_invalid_mode_forces_black();
```

## Acceptance Criteria

1. All existing Phase A and Phase B tests pass without modification.
2. Standard text mode (`mode=0`) output is pixel-identical to the pre-Phase C output
   on the BASIC READY screen.
3. ECM text mode (`mode=4`) selects `B0C/B1C/B2C/B3C` as background per character
   code bits 6–7 and uses only the lower 6 character code bits for the glyph address.
4. Standard bitmap mode (`mode=2`) reads pixel data from `bitmap_base + cell*8 + row_in_cell`
   and color from the video matrix byte (screen RAM), not from color RAM.
5. Multicolor bitmap mode (`mode=3`) renders four-color pixel pairs; adjacent pixel
   pairs within a character span two screen pixels each.
6. MCM text mode (`mode=1`) renders hires characters when `color_nib & 0x08 == 0` and
   multicolor pixel pairs when `color_nib & 0x08 != 0`, with the multicolor-mode color
   using only the lower 3 bits of the color nibble.
7. Invalid modes (`mode >= 5`) produce a fully black display area; the border color is
   unaffected.
8. Mode changes take effect immediately on the next call to `vicii_make_frame_snapshot`:
   no per-frame state caching of the mode bits.
9. All five new tests pass: `test_ecm_text_mode`, `test_standard_bitmap_mode`,
   `test_multicolor_bitmap_mode`, `test_mcm_text_mode`, `test_invalid_mode_forces_black`.

---

## What Is Not Implemented in This Phase

Do not implement any of the following:

- Hardware sprites (display, collision, priority) — Phase D/E
- Light pen — Phase F
- Open bus / last-byte behavior — Phase G
- Cycle steal integration (BA/AEC model) — Phase H
- Per-cycle mode-switching mid-character (the renderer is a whole-frame snapshot)
- Idle-state g-access fetch from `$3FFF` / `$39FF` — the snapshot renderer does not
  model idle state per-cycle; background color is used instead
- DRAM refresh counter

---

## Files Modified

| File | Change |
|------|--------|
| `src/machine/vicii.c` | Add 3 enum constants; add `mode`, `bitmap_base`, `b0c`–`b3c` variables in `vicii_make_frame_snapshot`; replace inner display-pixel block with 8-way mode switch; remove `background_color`/`background_index` locals |
| `tests/machine/test_c64_vicii.c` | Add 4 palette `#define`s; add 5 test functions; add 5 calls in `main()` |

No changes to `vicii.h`, `c64.c`, or any other file.
