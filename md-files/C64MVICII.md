# C64MVICII.md
# VIC-II Implementation Plan for c64m

## Purpose

This document is an intermediate-level implementation plan for completing the VIC-II
emulation in c64m. Each phase is sequenced by dependency order and is specific enough
to understand scope and intent, but each phase will require a refinement pass (producing
a coding-agent-ready phase document) before being handed to a coding agent.

## Reference

Primary source: Christian Bauer, "The MOS 6567/6569 video controller (VIC-II) and its
application in the Commodore 64" (1996). Authoritative for all timing, register, and
behavioral details cited here.

Secondary source: oxyron.de VIC-II register reference.

---

## Current State (as of Phase 14)

Already implemented:

- Register storage and mirroring across $D000–$D3FF
- Raster timing skeleton (PAL/NTSC line and frame counts)
- Frame generation and border/background fill
- 40×25 text rendering: screen RAM fetch, character ROM glyph fetch, color RAM fetch
- $D018 screen/charset pointer support
- Raster IRQ via $D011/$D012 (raster compare, RST8 bit)
- IRQ status/enable registers ($D019/$D01A)
- DEN, RSEL, CSEL, XSCROLL, YSCROLL stored but scroll not yet pixel-accurate
- VIC bank register wired through CIA 2 port A

Not yet implemented (scope of this plan):

- Accurate Bad Line detection and CPU cycle stealing
- Accurate g-access / s-access / p-access / r-access timing
- Pixel-accurate smooth scrolling (XSCROLL/YSCROLL)
- 24/25 row and 38/40 column border clamping (RSEL/CSEL)
- All graphics modes beyond standard text (MCM text, bitmap, ECM, invalid modes)
- Hardware sprites (all 8, all features)
- Sprite-background and sprite-sprite collision detection
- Sprite priority ($D01B)
- Light pen latch
- DRAM refresh counter (emulation-visible effects only)
- Open bus / last-byte-on-bus behavior for unused register reads

---

## Dependency Order Overview

```
Phase A  Raster timing accuracy & Bad Lines
Phase B  Pixel-accurate smooth scroll & display window clamping
Phase C  Graphics modes (multicolor text, bitmap, ECM, invalid)
Phase D  Sprites: display (all features)
Phase E  Sprites: collision detection & priority
Phase F  Light pen
Phase G  Open bus / last-byte behavior
Phase H  Cycle steal integration (BA/AEC model)
```

Phases A and B are prerequisites for everything else.
Phase C must precede D and E.
Phase F, G, H are independent and can be done in any order after A.

---

## Phase A — Raster Timing Accuracy & Bad Lines

### Goal

The raster counter and Bad Line logic must be cycle-accurate. This is the foundation
for all subsequent timing-dependent features.

### Properties

**Raster counter:**
- X counter: 0–62 (PAL, 63 cycles/line), 0–64 (NTSC R8, 65 cycles), 0–63 (NTSC R56A)
- Y counter: 0–311 (PAL), 0–262 (NTSC R8), same for R56A
- X and Y raster positions exposed via $D012 (bits 7–0 of Y) and $D011 bit 7 (Y bit 8)
- Writing $D012 sets the raster compare register; reading returns the current raster line

**Bad Line condition:**
- Active when: DEN=1 AND (raster_y & 7) == YSCROLL AND raster_y is in the visible display range (first bad line is raster 0x30 for PAL)
- On a Bad Line, the VIC performs 40 c-accesses to fetch the video matrix (character
  pointers from screen RAM and color nibbles from color RAM)
- The c-access window is cycles 15-54 (40 cycles). BA goes low 3 cycles before the
  window begins; exact CPU RDY/read-vs-write behavior is completed in Phase H.
- The fetched 40 bytes are stored in the internal 40×12-bit video matrix / color line buffer

**VC and RC (internal counters):**
- VC (Video Counter): 10-bit counter, 0–1023, incremented after each g-access; reset
  to 0 at the start of each frame (raster line 0)
- VCBASE: latched copy of VC at start of each Bad Line row; VC is loaded from VCBASE
  at the start of each Bad Line
- RC (Row Counter): 3-bit counter, 0–7, counts pixel rows within a character row;
  incremented at end of each display line; reset to 0 on a Bad Line

**Display state vs idle state:**
- Display state: active when RC is not 7 at the end of a line (i.e. a character row
  is in progress); g-accesses fetch real glyph data
- Idle state: when no character row is active; g-accesses fetch from $3FFF (or $39FF
  in ECM mode); the fetched byte is displayed using the idle data

### Acceptance Criteria

- Raster IRQ fires at the correct cycle within the correct raster line
- Bad Lines occur at the correct Y positions for both PAL and NTSC
- Bad Line c-accesses occupy exactly 40 cycles; Phase A may expose a conservative
  stall predicate, while Phase H owns exact BA/AEC/RDY integration.
- VC increments correctly across a frame; VCBASE/RC track character rows correctly
- A test program that reads $D012 in a busy loop produces the correct raster progression

---

## Phase B — Pixel-Accurate Smooth Scroll & Display Window Clamping

### Goal

XSCROLL and YSCROLL shift the display window contents by 0–7 pixels in each axis.
RSEL and CSEL clamp the visible display area to 24/25 rows and 38/40 columns.
These interact with the border unit.

### Properties

**YSCROLL (bits 2–0 of $D011):**
- Shifts the display window vertically by 0–7 raster lines
- Determines which raster lines are Bad Lines: (raster_y & 7) == YSCROLL
- Changes to YSCROLL mid-frame take effect immediately for Bad Line detection

**XSCROLL (bits 2–0 of $D016):**
- Shifts the display window horizontally by 0–7 pixels
- Implemented by delaying the start of g-access pixel output by XSCROLL pixel clocks
- The first pixel of each character row is output starting at a pixel-clock offset
  determined by XSCROLL

**RSEL ($D011 bit 3):**
- RSEL=1: 25 rows, top border ends at raster 51 (PAL), bottom border starts at raster 251
- RSEL=0: 24 rows, top border ends at raster 55, bottom border starts at raster 247
- Implemented in the border unit vertical flip-flop

**CSEL ($D016 bit 3):**
- CSEL=1: 40 columns, left border ends at pixel X=24, right border starts at X=344
- CSEL=0: 38 columns, left border ends at X=31, right border starts at X=335
- Implemented in the border unit horizontal flip-flop

**Border unit:**
- Separate vertical and horizontal flip-flops control border display
- Vertical flip-flop: set when raster reaches the top border compare value; cleared
  at bottom compare value; gated by RSEL
- Horizontal flip-flop: set/cleared at left/right pixel X compare values; gated by CSEL
- Border color ($D020) is output when either flip-flop is active
- Display output is suppressed (replaced by border color) when the border is active

### Acceptance Criteria

- YSCROLL=3 (default) produces the standard 25-row display starting at the correct raster line
- Changing YSCROLL by 1 shifts the entire display content by exactly 1 raster line
- XSCROLL=0 and XSCROLL=7 produce visible pixel shifts matching hardware behavior
- RSEL=0 produces a visible top/bottom border extension of 4 raster lines each
- CSEL=0 produces a visible left/right border extension of 7 pixels each
- FLD (flexible line distance) effect is achievable by changing YSCROLL each line

---

## Phase C — Graphics Modes

### Goal

Implement all 8 ECM/BMM/MCM combinations, including the 3 invalid modes.
Standard text mode already works; this phase adds the remaining 7.

### Mode Summary

Mode bits are ECM ($D011 bit 6), BMM ($D011 bit 5), MCM ($D016 bit 4).

**MCM text mode (ECM=0, BMM=0, MCM=1):**
- Characters with bit 3 of color nibble clear: rendered as standard text (hires, fg/bg)
- Characters with bit 3 of color nibble set: rendered as multicolor
  - Pixel pairs: 00=B0C, 01=B1C, 10=B2C, 11=color nibble bits 2–0
- Horizontal resolution halved (4 effective pixel pairs per 8 clock pixels)

**Standard bitmap mode (ECM=0, BMM=1, MCM=0):**
- 320×200 pixel grid; each 8×8 block has its own foreground and background color
- g-access fetches bitmap data (1 bit per pixel)
- Color comes from the video matrix byte: high nibble = foreground, low nibble = background
- Glyph address replaced by bitmap address: ((VC * 8) + RC) within the bitmap base

**Multicolor bitmap mode (ECM=0, BMM=1, MCM=1):**
- 160×200 effective pixels (pixel pairs, 4 colors per 8×8 block)
- Pixel pair 00=B0C, 01=video matrix high nibble, 10=video matrix low nibble,
  11=color RAM nibble

**ECM text mode (ECM=1, BMM=0, MCM=0):**
- Only 64 characters available (bits 6–7 of character code select background color)
- 4 background colors: B0C (bits 6–7=00), B1C (01), B2C (10), B3C (11)
- Glyph address: only lower 6 bits of character code used; upper 2 bits select BG color

**Invalid modes (ECM=1 with BMM or MCM set):**
- In the background/display layer, display pixels are forced to black (color index 0)
- Memory accesses still occur normally in the timing path (bus timing unchanged)
- Sprite behavior and collision handling are specified in Phase D/E
- Three invalid combinations: 101, 110, 111

### Acceptance Criteria

- Each mode can be selected by writing the appropriate bit combination to $D011/$D016
- A test screen in each mode produces visually correct output matching known screenshots
- Invalid modes produce a fully black display area (border color unaffected)
- Mode changes mid-frame take effect at the pixel level on the correct cycle

---

## Phase D — Sprites: Display

### Goal

Implement all 8 hardware sprites with full display feature support. No collision
detection yet (that is Phase E).

### Properties

**Data:**
- Each sprite is 24×21 pixels = 63 bytes, stored as 3 bytes per row, 21 rows
- Sprite data pointer: fetched from offsets $3F8–$3FF at the end of the current
  video matrix page
  (relative to the current VIC bank), one pointer per sprite
- Pointer × 64 = address of sprite data within the VIC bank
- p-access: 1 fetch per sprite (pointer byte)
- s-access: 3 fetches per sprite per active row (data bytes), stored in 24-bit sprite
  data buffers

**Position:**
- X: 9-bit (low 8 bits in $D000/$D002/…/$D00E; bit 8 from corresponding bit of $D010)
- Y: 8-bit ($D001/$D003/…/$D00F)
- Sprite is displayed when raster_y == M?Y (sprite row 0 begins) through M?Y+20
  (or M?Y+41 for Y-expanded sprites)

**Per-sprite feature flags:**
- Enable: $D015 bit N — sprite is not fetched or displayed if clear
- X-expand: $D01D bit N — doubles horizontal size (each pixel becomes 2 pixels wide,
  sprite becomes 48 pixels wide)
- Y-expand: $D017 bit N — doubles vertical size (each row repeated twice,
  sprite becomes 42 pixels tall)
  - Y-expand interacts with the sprite row counter: the row counter only increments
    on alternate raster lines when Y-expand is set
- Multicolor: $D01C bit N — changes pixel encoding:
  - Standard: 1 bit per pixel; 0=transparent, 1=sprite color ($D027+N)
  - Multicolor: 2 bits per pixel; 00=transparent, 01=MM0 ($D025), 10=sprite color,
    11=MM1 ($D026)

**Sprite display sequencer:**
- Each sprite has a 6-bit MOB data counter (MC), tracking byte position within the
  63-byte data block
- MC reset to 0 when the sprite's Y coordinate matches the raster line
- MC incremented after each s-access (3 per active raster line for non-Y-expanded;
  same 3 fetches but MC only advances on alternate lines for Y-expanded)

**Bus timing for sprites:**
- Sprite pointer and data fetches occur in specific cycles of each raster line
- BA goes low 3 cycles before the VIC needs the bus for sprite data fetches
- Sprites 0–7 are fetched in a fixed cycle window near the end/start of each line

### Acceptance Criteria

- All 8 sprites can be independently positioned and displayed
- X-expand doubles width correctly
- Y-expand doubles height correctly, including correct row-counter behavior
- Multicolor sprites display correct pixel pairs with MM0/MM1/sprite color/transparent
- Disabling a sprite ($D015) suppresses both display and memory fetches
- Sprites wrap correctly at X=0 and X=511 boundaries
- Sprites display above the background (priority handled in Phase E)

---

## Phase E — Sprites: Collision Detection & Priority

### Goal

Implement sprite-background and sprite-sprite collision registers and sprite display
priority over/under background graphics.

### Properties

**Sprite data priority ($D01B):**
- Each bit controls whether the corresponding sprite appears in front of (0) or behind
  (1) foreground background pixels
- "Foreground" means non-zero color pixels in the current graphics mode
- Border always appears in front of sprites

**Sprite-sprite collision ($D01E):**
- A bit is set when two sprites overlap with non-transparent pixels on the same raster line
- All sprites involved in a collision have their bits set simultaneously
- Register is read-only; automatically cleared on read
- IMMC interrupt fires if enabled in $D01A

**Sprite-data collision ($D01F):**
- A bit is set when a sprite overlaps a non-transparent foreground pixel in the
  background graphics layer
- Register is read-only; automatically cleared on read
- IMBC interrupt fires if enabled in $D01A

**Priority MUX logic (in display order, front to back):**
1. Border (always on top)
2. Sprites with $D01B bit = 0 (in sprite number order, 0 = highest)
3. Foreground background graphics pixels
4. Sprites with $D01B bit = 1
5. Background color / background graphics background pixels

**Collision detection notes:**
- Collision is detected on non-transparent pixels only
- Collision detection is performed even when a sprite is behind background ($D01B=1)
- Collision detection is performed even when DEN=0
- The first collision in a frame triggers the IRQ; subsequent collisions in the same
  frame still update the register but only trigger IRQ once (IRQ flag must be cleared first)

### Acceptance Criteria

- Two overlapping sprites set the correct bits in $D01E and trigger IMMC IRQ if enabled
- A sprite overlapping a foreground pixel sets the correct bit in $D01F and triggers IMBC
- Registers clear on read
- Sprites with $D01B=1 display behind foreground pixels but still detect collisions
- Priority order among sprites is strictly 0 > 1 > … > 7

---

## Phase F — Light Pen

### Goal

Implement the LP input latch and associated registers.

### Properties

- LP pin is shared with CIA 1 port B bit 4 (joystick port 1 fire) and keyboard matrix
- On a falling edge of LP, the VIC latches:
  - LPX ($D013): current X raster position ÷ 2 (pixel position / 2, 8-bit)
  - LPY ($D014): current Y raster line (8-bit, same as $D012)
- Only one latch per frame: once triggered, LP is ignored until the next frame
- ILP interrupt bit set in $D019; EILP enable in $D01A
- LPX and LPY are read-only; writes are ignored

### Acceptance Criteria

- Writing a falling edge to the LP-connected CIA bit latches correct X/Y values
- Subsequent LP edges in the same frame are ignored
- ILP interrupt fires if EILP is set in $D01A
- LPX and LPY read back the correct latched values

---

## Phase G — Open Bus / Last-Byte Behavior

### Goal

Reads from unused VIC register addresses and unused bits within valid registers should
return the last byte placed on the VIC data bus, not 0x00 or 0xFF uniformly.

### Properties

- Unused register addresses $D02F–$D03F: reads return $FF (all bits 1) per the
  Bauer reference; some real hardware returns the last VIC bus byte instead
- Unused bits within valid registers: per oxyron reference, unused bits read back as 1
  (e.g. upper 4 bits of color registers, upper bits of $D016, $D019, $D01A)
- $D01E and $D01F: read-only, auto-cleared on read; writes have no effect
- The "last byte on bus" behavior: on many C64 boards, reading an open address in the
  $DE00 area or unused VIC registers returns the last byte the VIC fetched during its
  most recent memory access; this is a secondary behavior and may be deferred to a
  later accuracy pass

### Implementation Note

The minimum correct behavior is: unused bits return 1, $D02F–$D03F return $FF.
Full last-byte-on-bus tracking is optional for now.

### Acceptance Criteria

- Reading $D02F through $D03F returns $FF
- Unused bits within valid registers (e.g. bits 7–4 of $D020–$D02E) read back as 1.
  For $D019, bits 6–4 read as 1, while bit 7 reflects the aggregate VIC IRQ state.
- $D01E and $D01F clear on read and cannot be written

---

## Phase H — Cycle Steal Integration (BA/AEC Model)

### Goal

Connect VIC's BA signal to the CPU's RDY line so that Bad Line c-accesses and sprite
fetch windows actually stall the 6510. This is the cycle-accuracy prerequisite for
demos and games that depend on precise timing.

### Properties

**BA signal behavior:**
- BA is normally high (CPU runs freely)
- BA goes low 3 cycles before any VIC bus takeover (Bad Line c-accesses or sprite fetches)
- The CPU is stalled on read cycles when BA is low; write cycles continue
- The 6510 never executes more than 3 consecutive write cycles, so BA going low 3
  cycles in advance guarantees the CPU is halted before the VIC needs the bus

**AEC signal:**
- Follows phi2: normally low in phi1 (VIC accesses), high in phi2 (CPU accesses)
- During VIC bus takeover: AEC remains low in phi2 as well, preventing CPU from
  driving the address bus

**What this affects:**
- All code that relies on cycle-counted raster timing (raster bars, etc.)
- Sprite multiplexers (rely on exact cycle counts per line)
- Any program that reads/writes registers in a tight raster loop

**Implementation approach:**
- The machine's tick() function must consult a vicii_ba_active() predicate before
  advancing the CPU; if BA is low and the CPU's next cycle is a read, the CPU tick
  is suppressed (the cycle still counts for VIC timing purposes)
- BA state must be computed based on the VIC's current raster X position and which
  sprites are active, not derived from the CPU's state

### Acceptance Criteria

- Bad Line stall cycles match the theoretical 40-cycle steal per bad line
- A cycle-counted raster routine produces stable raster bars at the correct screen positions
- Sprite fetch stalls match expected cycle counts for active sprites
- Existing boot and keyboard tests continue to pass (stalls must not break normal execution)

---

## Suggested Phase Sequence for c64m

Given the project's philosophy of vertical slices and the current state (Phase 14
complete, BASIC screen working), the suggested order is:

```
1. Phase A  — Raster timing & Bad Lines          (correctness foundation)
2. Phase B  — Smooth scroll & border clamping    (display accuracy)
3. Phase C  — Graphics modes                     (content variety)
4. Phase D  — Sprite display                     (visual completeness)
5. Phase E  — Sprite collision & priority        (game compatibility)
6. Phase H  — Cycle steal / BA integration       (timing accuracy)
7. Phase F  — Light pen                          (peripheral, low priority)
8. Phase G  — Open bus behavior                  (accuracy polish)
```

Phases F and G can be done at any point after Phase A and are low-priority for most
software compatibility.

Phase H (cycle stealing) is placed after sprite display because sprites are the primary
driver of mid-line BA events. However, Bad Line stealing (the c-access window) can be
implemented earlier as part of Phase A if desired, since it does not depend on sprites.

---

## Notes for Phase Document Authors

When refining any phase above into a coding-agent-ready document, include:

- Exact register addresses, bit masks, and read/write behavior for all registers touched
- Which existing c64m structs and files are modified
- Which new structs or fields are added
- Precise acceptance criteria expressed as observable emulator behavior or test conditions
- Any interaction with the threading model (BA signal crosses machine→runtime boundary)
- Reference to the specific section of the Bauer VIC-II article for that feature
