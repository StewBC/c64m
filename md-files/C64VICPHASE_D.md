# C64VICPHASE_D.md
# VIC-II Phase D — Sprites: Display (Coding-Agent Implementation Guide)

## Prerequisites

Phases A, B, and C must be complete. The following must already exist in the codebase:
- `vicii` struct with `vc`, `vc_base`, `rc`, `display_state`, `bad_line`, `video_matrix[40]`,
  `color_line[40]`, `irq_status`, `irq_enable` fields (Phase A)
- Border geometry with RSEL/CSEL-gated flip-flops (Phase B)
- All 8 graphics modes including ECM, bitmap, multicolor (Phase C)

Reference: Bauer, "The MOS 6567/6569 video controller (VIC-II) and its application in
the Commodore 64" (1996), §3.7 (sprites), §3.7.2 (timing), §3.8 (registers).

---

## Scope

Implement all 8 hardware sprites with full display support:
- Position (9-bit X with wraparound, 8-bit Y)
- Enable ($D015)
- X-expand ($D01D), Y-expand ($D017)
- Hires and multicolor modes ($D01C, $D025, $D026, $D027–$D02E)
- Sprite pointer (p-access) and data (s-access) fetches
- Sprites always displayed above background (priority $D01B is Phase E)

Collision detection and sprite-background priority are **not** in scope here (Phase E).

---

## Register Map

All registers live in `vicii.registers[0x00..0x3F]`. Addresses $D000–$D02E
(register indices 0x00–0x2E) are used by sprites.

| Register | Index | Description |
|----------|-------|-------------|
| $D000 | 0x00 | Sprite 0 X (bits 7–0) |
| $D001 | 0x01 | Sprite 0 Y |
| $D002 | 0x02 | Sprite 1 X |
| $D003 | 0x03 | Sprite 1 Y |
| … | … | Sprites 2–6: same pattern (X at 0x04,0x06,0x08,0x0A,0x0C; Y at 0x05,0x07,0x09,0x0B,0x0D) |
| $D00E | 0x0E | Sprite 7 X |
| $D00F | 0x0F | Sprite 7 Y |
| $D010 | 0x10 | Sprite X MSBs — bit N is bit 8 of sprite N's X coordinate |
| $D015 | 0x15 | Sprite enable — bit N enables sprite N |
| $D017 | 0x17 | Sprite Y-expand — bit N doubles sprite N's height (42 lines) |
| $D01C | 0x1C | Sprite multicolor — bit N selects multicolor mode for sprite N |
| $D01D | 0x1D | Sprite X-expand — bit N doubles sprite N's width (48 pixels) |
| $D025 | 0x25 | Sprite multicolor 0 (MM0, shared by all sprites in MCM mode) |
| $D026 | 0x26 | Sprite multicolor 1 (MM1, shared by all sprites in MCM mode) |
| $D027 | 0x27 | Sprite 0 color |
| $D028 | 0x28 | Sprite 1 color |
| … | … | … |
| $D02E | 0x2E | Sprite 7 color |

None of these require special read/write handling beyond the default pass-through in
`vicii_write_register` / `vicii_read_register`. The existing `default:` case in
`vicii_write_register` already stores them.

---

## Coordinate System

The frame buffer is `C64_FRAME_WIDTH` × `C64_FRAME_HEIGHT` = 384 × 272 pixels.

Frame pixel X maps 1:1 to VIC-II screen dot X for the visible range:
- Frame pixel 0 = VIC dot 0
- Frame pixel 24 = VIC dot 24 = left edge of 40-column display (CSEL=1)
- Frame pixel 344 = VIC dot 344 = right edge of 40-column display (CSEL=1)
- Frame pixels ≥ 384 are outside the frame (VIC dots 384–503 are off-screen)

Frame pixel Y maps 1:1 to VIC raster line Y for the visible range:
- Frame row 0 = raster line 0
- Frame row 51 = raster line 51 = top of 25-row display (RSEL=1)
- Frame row 272 = off-screen (PAL lines 272–311 are below the frame)

Sprite X is a 9-bit coordinate (0–511). Sprite pixels are evaluated in modulo-512
horizontal VIC space, so sprites near the right edge can wrap and appear again near
frame X=0. For example, a 24-pixel-wide sprite at X=500 displays its last 12 pixels
at frame X=0..11. Sprites whose modulo-512 covered range does not intersect frame
X=0..383 are off-screen in this frame.

Sprite Y is an 8-bit coordinate (0–255). Values ≥ 272 (PAL) are off-screen in the
384×272 frame, except that Y=255 still activates and can display within the bottom
visible rows if the frame includes those raster lines. Vertical wraparound is not in
scope for Phase D.

---

## Sprite Geometry

**Hires sprite (MCM bit = 0):**
- 24 pixels wide × 21 rows high
- 3 bytes per row, 1 bit per pixel
- Pixel bit: `bit = (sprite_data[n][dx/8] >> (7 - dx%8)) & 1`
  - `0` = transparent
  - `1` = sprite color ($D027+n)

**X-expanded hires sprite:**
- 48 pixels wide × 21 rows high
- Same 3 bytes per row; each bit maps to 2 consecutive pixels
- `bit_pos = dx / 2`, then same extraction

**Multicolor sprite (MCM bit = 1):**
- 24 pixels wide × 21 rows high
- 3 bytes per row, pixel pairs (2 bits each); each pair = 2 consecutive pixels
- `pair_index = dx / 2` (0–11); `pair_value = (sprite_data[n][pair_index*2/8] >> (6 - (pair_index*2)%8)) & 3`
  - `0b00` = transparent
  - `0b01` = MM0 (`registers[0x25] & 0x0F`)
  - `0b10` = sprite color (`registers[0x27+n] & 0x0F`)
  - `0b11` = MM1 (`registers[0x26] & 0x0F`)

**X-expanded multicolor sprite:**
- 48 pixels wide
- `pair_index = dx / 4` (0–11); same pair extraction

**Y-expansion** doubles height: each of the 21 rows is displayed on 2 consecutive raster
lines. The sprite occupies 42 raster lines total.

---

## Memory Accesses

Phase D uses a logical fetch model for sprite display correctness. It performs the
same pointer and data reads the VIC-II needs, but it does not model the exact hardware
cycle positions of p-accesses or s-accesses. Phase H owns BA/AEC/RDY timing and
cycle-accurate bus stealing.

### VIC Bank Base

The VIC-II sees a 16 KB window into system RAM determined by CIA 2 port A bits 1–0
(inverted). Add this function to `c64_bus.c` / `c64_bus.h`:

```c
/* Returns the VIC bank base address (0x0000, 0x4000, 0x8000, or 0xC000). */
uint16_t c64_bus_vic_bank_base(const c64_bus_t *bus) {
    uint8_t pa;
    if (!bus->cia2) return 0;
    pa = cia_read_register(bus->cia2, 0xDD00);
    return (uint16_t)(((~pa) & 3u) * 0x4000u);
}
```

Add the declaration to `c64_bus.h`.

### Sprite Pointer Fetch (p-access)

Each sprite has a 1-byte pointer stored at the last 8 bytes of the current screen RAM
page within the VIC bank:

```
pointer_addr = (vic_bank_base + screen_base + 0x03F8 + n) & 0xFFFF
```

Where `screen_base = ((registers[0x18] >> 4) & 0x0F) * 0x0400`.

`pointer = c64_bus_vic_read_ram(bus, pointer_addr)`

### Sprite Data Fetch (s-access)

Three bytes of sprite data for the current row:

```
sprite_data_base = (vic_bank_base + (uint16_t)pointer * 64u) & 0xFFFF
sprite_data[n][0] = c64_bus_vic_read_ram(bus, (sprite_data_base + mc[n])     & 0xFFFF)
sprite_data[n][1] = c64_bus_vic_read_ram(bus, (sprite_data_base + mc[n] + 1) & 0xFFFF)
sprite_data[n][2] = c64_bus_vic_read_ram(bus, (sprite_data_base + mc[n] + 2) & 0xFFFF)
```

`mc[n]` is the byte offset within the 63-byte sprite data block (0, 3, 6, … 60).

### Char ROM Transparency

The VIC-II sprite fetch always reads from RAM, even in regions the CPU sees as ROM.
`c64_bus_vic_read_ram()` already reads `bus->ram[address]` directly — no change needed.

---

## Struct Changes: `vicii.h`

Add the following fields to `struct vicii` after the existing Phase A fields:

```c
    /* Phase D: per-sprite state */
    uint8_t  sprite_mc[8];          /* next byte offset into 63-byte sprite block (0,3,6…60) */
    bool     sprite_active[8];      /* sprite sequencer remains active for future lines */
    bool     sprite_visible[8];     /* sprite has valid fetched data for the current line */
    bool     sprite_y_exp_ff[8];    /* Y-expand flip-flop; governs when mc advances */
    uint8_t  sprite_data[8][3];     /* current row: 3 fetched data bytes */
```

---

## Changes to `vicii.c`

### 1. New Enum Constants

Add to the anonymous `enum` at the top of `vicii.c`:

```c
    VICII_REG_SPR_X_MSB     = 0x10,
    VICII_REG_SPR_ENABLE    = 0x15,
    VICII_REG_SPR_Y_EXPAND  = 0x17,
    VICII_REG_SPR_MULTICOLOR = 0x1C,
    VICII_REG_SPR_X_EXPAND  = 0x1D,
    VICII_REG_SPR_MM0       = 0x25,
    VICII_REG_SPR_MM1       = 0x26,
```

### 2. `vicii_reset()`

Zero-initialize the new fields immediately after the existing Phase A initializations:

```c
    memset(v->sprite_mc,       0, sizeof(v->sprite_mc));
    memset(v->sprite_active,   0, sizeof(v->sprite_active));
    memset(v->sprite_visible,  0, sizeof(v->sprite_visible));
    memset(v->sprite_y_exp_ff, 0, sizeof(v->sprite_y_exp_ff));
    memset(v->sprite_data,     0, sizeof(v->sprite_data));
```

### 3. Sprite Fetch Helper

Add a new static function before `vicii_step_cycle`:

```c
static void vicii_fetch_sprites(vicii *v, const c64_bus_t *bus) {
    uint16_t vic_bank;
    uint16_t screen_base;
    uint8_t  enable;
    int      n;

    memset(v->sprite_visible, 0, sizeof(v->sprite_visible));

    if (!bus) return;

    vic_bank    = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0Fu) * 0x0400u);
    enable      = v->registers[VICII_REG_SPR_ENABLE];

    for (n = 0; n < 8; n++) {
        uint8_t  spr_y     = v->registers[1 + n * 2];         /* $D001, $D003, ... */
        uint32_t raster_y  = v->timing.raster_line;
        bool     y_expand  = (v->registers[VICII_REG_SPR_Y_EXPAND] >> n) & 1u;

        /* Activate sprite on the raster line matching its Y coordinate. */
        if ((enable >> n) & 1u) {
            if (raster_y == (uint32_t)spr_y) {
                v->sprite_mc[n]       = 0;
                v->sprite_active[n]   = true;
                v->sprite_y_exp_ff[n] = false;
            }
        } else {
            v->sprite_active[n]  = false;
            v->sprite_visible[n] = false;
        }

        if (v->sprite_active[n]) {
            uint8_t mc = v->sprite_mc[n];
            bool    advance_mc;

            /* Y-expand flip-flop: advance MC only on alternate lines. */
            if (y_expand) {
                advance_mc             = v->sprite_y_exp_ff[n]; /* advance if ff was true */
                v->sprite_y_exp_ff[n] ^= 1u;
            } else {
                advance_mc = true;
            }

            /* p-access: read sprite pointer. */
            uint16_t ptr_addr = (uint16_t)((vic_bank + screen_base + 0x03F8u + (uint16_t)n) & 0xFFFFu);
            uint8_t  pointer  = c64_bus_vic_read_ram(bus, ptr_addr);

            /* s-accesses: read 3 data bytes for the current row. */
            uint16_t data_base = (uint16_t)((vic_bank + (uint16_t)pointer * 64u) & 0xFFFFu);
            v->sprite_data[n][0] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc)      & 0xFFFFu));
            v->sprite_data[n][1] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 1u) & 0xFFFFu));
            v->sprite_data[n][2] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 2u) & 0xFFFFu));
            v->sprite_visible[n] = true;

            /* Do not clear sprite_visible when this fetch consumed the last row. The
             * current line still needs to render the fetched bytes. Clearing
             * sprite_active only prevents another fetch on the next line.
             */
            if (advance_mc) {
                mc = (uint8_t)(mc + 3u);
                if (mc >= 63u) {
                    v->sprite_active[n] = false;
                } else {
                    v->sprite_mc[n] = mc;
                }
            }
        }
    }
}
```

### 4. `vicii_step_cycle()` — Call Sprite Fetch at Cycle 0

In the `if (cycle == 0)` block at the top of `vicii_step_cycle`, add the sprite fetch
call **after** the existing bad-line and IRQ logic:

```c
    if (cycle == 0) {
        /* … existing raster IRQ and bad-line code … */

        vicii_fetch_sprites(v, bus);   /* Phase D: fetch sprite data for this line */
    }
```

### 5. Sprite Pixel Helper


Add a helper for horizontal wraparound before `vicii_sprite_pixel()`:

```c
static int vicii_sprite_dx_wrapped(uint32_t frame_x, uint16_t spr_x) {
    int dx = (int)frame_x - (int)(spr_x & 0x01FFu);
    if (dx < 0) dx += 512;
    return dx;
}
```

Add a static function that returns the ARGB pixel for a sprite at a given frame X
offset, or `0` for transparent. This is used by both the live renderer and the
snapshot renderer.

```c
/*
 * Returns the ARGB pixel for sprite n at horizontal frame offset dx (0-based from
 * the sprite's screen X position). Returns 0 (fully transparent / black with alpha 0)
 * if the pixel is transparent. Caller checks alpha or a separate bool.
 *
 * Use the returned value only when this function returns true.
 */
static bool vicii_sprite_pixel(
    const vicii *v,
    int          n,
    int          dx,
    uint32_t    *out_pixel)
{
    bool    x_expand  = (v->registers[VICII_REG_SPR_X_EXPAND]  >> n) & 1u;
    bool    multicolor = (v->registers[VICII_REG_SPR_MULTICOLOR] >> n) & 1u;
    int     spr_width = x_expand ? 48 : 24;

    if (dx < 0 || dx >= spr_width) return false;

    if (multicolor) {
        /* Two bits per logical pixel; each logical pixel = 2 screen pixels (×2 for x_expand) */
        int pair_index = x_expand ? (dx / 4) : (dx / 2);  /* 0-11 */
        int bit_shift  = 6 - (pair_index * 2) % 8;
        uint8_t pair   = (v->sprite_data[n][(pair_index * 2) / 8] >> bit_shift) & 3u;
        switch (pair) {
        case 0u:  return false;   /* transparent */
        case 1u:  *out_pixel = vicii_palette_argb[v->registers[VICII_REG_SPR_MM0] & 0x0Fu]; return true;
        case 2u:  *out_pixel = vicii_palette_argb[v->registers[0x27u + (uint8_t)n] & 0x0Fu]; return true;
        default:  *out_pixel = vicii_palette_argb[v->registers[VICII_REG_SPR_MM1] & 0x0Fu]; return true;
        }
    } else {
        /* One bit per pixel */
        int     bit_pos   = x_expand ? (dx / 2) : dx;
        uint8_t bit       = (v->sprite_data[n][bit_pos / 8] >> (7 - bit_pos % 8)) & 1u;
        if (!bit) return false;
        *out_pixel = vicii_palette_argb[v->registers[0x27u + (uint8_t)n] & 0x0Fu];
        return true;
    }
}
```

### 6. `vicii_live_pixel()` — Composite Sprites

At the **end** of `vicii_live_pixel`, just before the final `return` of the background
pixel, add sprite compositing. Replace the existing function's return structure so
that:

1. The background pixel is computed as before (stored in a local `uint32_t pixel`).
2. Sprites are overlaid (sprite 0 has highest priority; checked first and stops on hit).
3. The final pixel is returned.

Add this block immediately before the `return` of the background pixel result in
every mode's `case` branch. The cleanest approach is to restructure the function so
all modes store their result in a local variable `pixel`, then composite sprites once
at the bottom:

```c
    /* After computing `pixel` from the background layer: */

    /* Phase D: sprite overlay (sprite 0 = highest priority) */
    if (!vborder) {
        uint8_t enable = v->registers[VICII_REG_SPR_ENABLE];
        for (int n = 0; n < 8; n++) {
            if (!((enable >> n) & 1u)) continue;
            if (!v->sprite_visible[n]) continue;

            uint16_t spr_x = (uint16_t)(v->registers[(uint16_t)(n * 2)] |
                             ((v->registers[VICII_REG_SPR_X_MSB] >> n & 1u) << 8));
            int dx = vicii_sprite_dx_wrapped(x, spr_x);
            uint32_t spr_pixel;
            if (vicii_sprite_pixel(v, n, dx, &spr_pixel)) {
                pixel = spr_pixel;
                break;
            }
        }
    }

    return pixel;
```

**Refactoring note:** The current `vicii_live_pixel` returns directly from inside each
`case`. Before adding the sprite overlay, refactor so that each case assigns to a
local `uint32_t pixel` variable and the single `return pixel` is at the bottom.
The `return vicii_palette_argb[border]` for border pixels should remain an early
return before the mode switch.

The border check must guard sprite rendering: sprites are not visible inside the
border (the border always wins). The `vborder || hborder` path returns early before
this block, so only guard against `vborder` explicitly for the horizontal border case.
Actually, if `vborder || hborder` → early return with border color, the sprite block
is never reached when either border is active. That is the correct behavior.

### 7. `vicii_make_frame_snapshot()` — Sprite Rendering

The snapshot function renders a complete frame without per-cycle state. Sprite data
must be computed per-raster-line inline.

**Add a per-line sprite simulation helper:**

```c
typedef struct {
    bool     active;
    uint8_t  data[3];
} vicii_snapshot_sprite_row;

static void vicii_snapshot_sprite_line(
    const vicii                  *v,
    const c64_bus_t              *bus,
    uint32_t                      raster_y,
    vicii_snapshot_sprite_row    *rows   /* output array of 8 */
) {
    uint16_t vic_bank;
    uint16_t screen_base;
    uint8_t  enable;
    int      n;

    for (n = 0; n < 8; n++) {
        rows[n].active = false;
    }

    if (!bus) return;

    vic_bank    = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0Fu) * 0x0400u);
    enable      = v->registers[VICII_REG_SPR_ENABLE];

    for (n = 0; n < 8; n++) {
        if (!((enable >> n) & 1u)) continue;

        uint8_t  spr_y    = v->registers[1 + n * 2];
        bool     y_expand = (v->registers[VICII_REG_SPR_Y_EXPAND] >> n) & 1u;
        int      dy       = (int)raster_y - (int)spr_y;

        if (dy < 0) continue;

        int sprite_row = y_expand ? (dy / 2) : dy;
        if (sprite_row > 20) continue;

        uint8_t  mc       = (uint8_t)(sprite_row * 3u);
        uint16_t ptr_addr = (uint16_t)((vic_bank + screen_base + 0x03F8u + (uint16_t)n) & 0xFFFFu);
        uint8_t  pointer  = c64_bus_vic_read_ram(bus, ptr_addr);
        uint16_t data_base = (uint16_t)((vic_bank + (uint16_t)pointer * 64u) & 0xFFFFu);

        rows[n].data[0] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc)     & 0xFFFFu));
        rows[n].data[1] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 1u) & 0xFFFFu));
        rows[n].data[2] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 2u) & 0xFFFFu));
        rows[n].active  = true;
    }
}
```

**In the per-pixel loop inside `vicii_make_frame_snapshot()`:**

Before the `for (x …)` loop, add per-line sprite fetch:

```c
        vicii_snapshot_sprite_row spr_rows[8];
        if (!vborder) {
            vicii_snapshot_sprite_line(v, bus, y, spr_rows);
        }
```

After computing `pixel` from the background layer, add sprite compositing (mirrors
the live-pixel logic, but uses `spr_rows[n].data` instead of `v->sprite_data[n]`):

```c
            /* Phase D: sprite overlay */
            if (!vborder && !hborder) {
                uint8_t enable_reg = v->registers[VICII_REG_SPR_ENABLE];
                for (int n = 0; n < 8; n++) {
                    if (!((enable_reg >> n) & 1u)) continue;
                    if (!spr_rows[n].active) continue;

                    uint16_t spr_x = (uint16_t)(v->registers[(uint16_t)(n * 2)] |
                                     ((v->registers[VICII_REG_SPR_X_MSB] >> n & 1u) << 8));
                    int dx = vicii_sprite_dx_wrapped(x, spr_x);

                    /* Inline vicii_sprite_pixel using spr_rows[n].data */
                    bool    x_expand   = (v->registers[VICII_REG_SPR_X_EXPAND]  >> n) & 1u;
                    bool    mc_mode    = (v->registers[VICII_REG_SPR_MULTICOLOR] >> n) & 1u;
                    int     spr_width  = x_expand ? 48 : 24;
                    if (dx < 0 || dx >= spr_width) continue;

                    uint32_t spr_pixel = 0;
                    bool     opaque    = false;

                    if (mc_mode) {
                        int     pair_index = x_expand ? (dx / 4) : (dx / 2);
                        int     bit_shift  = 6 - (pair_index * 2) % 8;
                        uint8_t pair       = (spr_rows[n].data[(pair_index * 2) / 8] >> bit_shift) & 3u;
                        switch (pair) {
                        case 1u: spr_pixel = vicii_palette_argb[v->registers[VICII_REG_SPR_MM0] & 0x0Fu]; opaque = true; break;
                        case 2u: spr_pixel = vicii_palette_argb[v->registers[0x27u + (uint8_t)n] & 0x0Fu]; opaque = true; break;
                        case 3u: spr_pixel = vicii_palette_argb[v->registers[VICII_REG_SPR_MM1] & 0x0Fu]; opaque = true; break;
                        default: break;
                        }
                    } else {
                        int     bit_pos = x_expand ? (dx / 2) : dx;
                        uint8_t bit     = (spr_rows[n].data[bit_pos / 8] >> (7 - bit_pos % 8)) & 1u;
                        if (bit) {
                            spr_pixel = vicii_palette_argb[v->registers[0x27u + (uint8_t)n] & 0x0Fu];
                            opaque    = true;
                        }
                    }

                    if (opaque) {
                        pixel = spr_pixel;
                        break;   /* sprite 0 wins; stop checking lower-priority sprites */
                    }
                }
            }
```

The `vicii_snapshot_sprite_line` call must be placed outside the inner `x` loop but
inside the `y` loop, and must be skipped when `vborder` is true for that `y`.

---

## Y-Expand Correctness - Summary of the Flip-Flop Model

The flip-flop `sprite_y_exp_ff[n]` is initialized to `false` when a sprite activates
(raster_y matches spr_y). Each line that the sprite is active:

1. Clear `sprite_visible[n]` at the start of the line.
2. Fetch sprite data using current `mc[n]` and set `sprite_visible[n] = true`.
3. `advance_mc = sprite_y_exp_ff[n]` for Y-expanded sprites, or `true` for non-expanded sprites.
4. If Y-expanded, toggle `sprite_y_exp_ff[n]` after reading it.
5. If `advance_mc`: compute `next_mc = mc[n] + 3`. If `next_mc >= 63`, clear
   `sprite_active[n]` for the next line, but leave `sprite_visible[n]` true for the
   current line. Otherwise store `next_mc` in `sprite_mc[n]`.

This produces the correct behavior:
- Line 0 relative (first active line): ff=false -> advance=false -> row 0 displayed
- Line 1 relative: ff=true -> advance=true -> row 0 displayed again, next line uses row 1
- ...
- Line 41 relative: ff=true -> advance=true -> row 20 displayed again, sprite deactivates for the next line

Non-Y-expanded sprites always use `advance_mc = true`; their last row still displays
because `sprite_visible[n]` remains true for the line that fetched bytes 60..62.

---

## Edge Cases and Constraints

**Sprite mid-line register writes:** Phase D samples sprite enable, position, expansion,
multicolor, and color registers through the simplified renderer state. Exact hardware
mid-line effects are not required. For this phase, it is acceptable for changes made
after cycle 0 to affect the next rendered line rather than the current pixel. The main
requirement is stable line-level rendering for normally configured sprites.

**Sprite disable mid-line:** `$D015` is checked at cycle 0 each line. A sprite disabled
mid-line may continue displaying until end of that line (simplified; real hardware
deactivates immediately). This is acceptable for Phase D.

**Sprite Y=255 wraparound:** Sprites set to Y=255 activate on raster line 255. In the
384×272 frame this is in-bounds. No special handling needed.

**Sprite X wraparound:** Horizontal sprite coverage is modulo 512. Compute `dx` with
`vicii_sprite_dx_wrapped()`, then let `vicii_sprite_pixel()` reject pixels where
`dx >= spr_width`. This supports sprites at X=488..511 wrapping into frame X=0..
without adding special cases to the pixel decoder.

**Sprite data address in character ROM region:** `c64_bus_vic_read_ram` reads from
`bus->ram` always; it bypasses the character ROM remapping seen by the CPU. This is
correct hardware behavior.

**VIC bank and char ROM:** In VIC banks 1 and 3, the VIC-II sees the character ROM at
offset $1000–$1FFF within the bank (addresses $1000 and $9000 in absolute RAM). This
is relevant for background character fetches but generally not for sprite data. Phase D
does not need to handle character ROM substitution; it can be deferred.

**Sprites behind the border:** Hardware sprites are always hidden behind the border.
The `vborder || hborder → return border_color` early return in `vicii_live_pixel`
already guarantees this since the sprite overlay block is never reached. No additional
logic needed.

---

## `vicii_step_cycle()` BA Signal for Sprite Fetches

Phase H owns cycle-accurate BA/RDY integration. For Phase D, do **not** assert
`v->timing.ba_low` for sprite fetches. The simplified fetch (all at cycle 0) is
sufficient for display correctness without cycle stealing.

---

## Acceptance Criteria

1. Enabling a sprite at $D015 and placing it with X/Y coordinates causes it to appear
   at the correct screen position.
2. All 8 sprites can be independently positioned and displayed simultaneously.
3. Horizontal wraparound works: sprites positioned near X=511 reappear at frame X=0
   with the correct clipped/wrapped pixels for both normal and X-expanded width.
4. X-expand ($D01D): a sprite with X-expand set is exactly 48 pixels wide, with each
   original pixel doubled horizontally.
5. Y-expand ($D017): a sprite with Y-expand set is exactly 42 raster lines tall, with
   each original row repeated twice.
6. Multicolor ($D01C): pixel pairs map to transparent / MM0 / sprite color / MM1
   correctly; combined X+Y expand also works correctly.
7. Clearing bit N of $D015 suppresses sprite N entirely (no pixels displayed, no
   effect on other sprites).
8. Sprites are rendered above the background graphics layer in all 5 valid graphics
   modes (Phase E will add priority control via $D01B).
9. Sprites are not visible inside the border (both vertical and horizontal).
10. The BASIC screen (40×25 text mode, default after boot) continues to render
   correctly with no visual regressions.
11. `vicii_make_frame_snapshot()` and the live frame renderer (`vicii_step_cycle` +
    `vicii_render_live_cycle`) produce visually matching sprite output.

---

## File Change Summary

| File | Change |
|------|--------|
| `src/machine/vicii.h` | Add `sprite_mc[8]`, `sprite_active[8]`, `sprite_visible[8]`, `sprite_y_exp_ff[8]`, `sprite_data[8][3]` to `struct vicii` |
| `src/machine/vicii.c` | Add sprite register enum constants; zero-init new fields in `vicii_reset()`; add `vicii_fetch_sprites()`, `vicii_sprite_dx_wrapped()`, and `vicii_sprite_pixel()` statics; call `vicii_fetch_sprites()` at cycle 0 in `vicii_step_cycle()`; refactor `vicii_live_pixel()` to use local `pixel` var and add sprite overlay; add `vicii_snapshot_sprite_line()` and sprite overlay loop in `vicii_make_frame_snapshot()` |
| `src/machine/c64_bus.h` | Add `uint16_t c64_bus_vic_bank_base(const c64_bus_t *bus)` declaration |
| `src/machine/c64_bus.c` | Add `c64_bus_vic_bank_base()` implementation |
