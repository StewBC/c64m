# C64VICPHASE_G.md
# VIC-II Phase G — Open Bus / Last-Byte Behavior

## Status

Phase F (Light Pen) is skipped. This document picks up directly from VIC-II Phase E
(sprite priority and collisions), which is complete per STATUS.md.

## Required Reading Order

```text
1. AGENTS.md
2. C64MVICII.md (Phase G section, plus the "Notes for Phase Document Authors" section)
3. STATUS.md
```

Do not read this document in isolation. The acceptance criteria below assume the
architecture, threading, and snapshot rules in AGENTS.md are already understood.

## Goal

Reads from unused VIC-II register addresses, and reads of unused bits within otherwise
valid registers, must return defined values (1-bits / $FF) rather than 0x00 or
uninitialized storage. This is an accuracy/polish pass over existing register read
paths — it does not change any rendering, timing, or bus-fetch behavior.

Reference: Christian Bauer, "The MOS 6567/6569 video controller (VIC-II) and its
application in the Commodore 64" (1996), register map section. Secondary: oxyron.de
VIC-II register reference. The table below is the required Phase G behavior. If an
implementer notices a conflict with Bauer during code review, Bauer remains
authoritative per C64MVICII.md and the discrepancy must be called out in STATUS.md
before changing the table-driven behavior.

## In Scope

1. **Unused register block $D02F–$D03F**: reads return `$FF`. This range is mirrored
   the same way the rest of $D000–$D3FF is currently mirrored (per the existing
   register mirroring implementation noted in STATUS.md) — Phase G must preserve
   that mirroring, not bypass it.
2. **Unused bits within valid, already-implemented registers**: every register from
   $D000–$D02E must follow the audit table below. Any bit position that is not backed
   by real VIC-II state must read as 1, regardless of what was last written to it.
3. **Regression re-verification** of unused-bit behavior already implemented in prior
   phases, specifically:
   - $D019: bits 6:4 read as 1; bit 7 reflects the aggregate enabled-pending VIC IRQ
     summary (already implemented — confirm with a dedicated test, do not reimplement)
   - $D01A: high nibble reads as 1 (already implemented — confirm with a dedicated test)
   - $D01E (sprite-sprite collision) and $D01F (sprite-background collision): read
     returns and clears the latch; writes are ignored (already implemented per Phase E
     — confirm with a dedicated test, do not reimplement)

   These three bullets are **test additions only**. If a test already exists that
   covers the exact behavior, do not duplicate it — link/extend it instead and note
   that in STATUS.md.

## Out of Scope (Do Not Implement)

- Full last-byte-on-bus tracking (the VIC's last fetched byte being returned for open
  addresses) is not part of the required Phase G handoff. Do not attempt it unless
  the human maintainer explicitly authorizes the stretch goal described below.
- Any change to which VIC-II addresses are mirrored or how mirroring is computed.
- Any change to g-access/c-access/p-access/s-access fetch timing or values.
- Light pen ($D013/$D014/ILP/EILP) — that is Phase F, explicitly skipped.
- Any rendering or sprite behavior change. This phase only touches register read paths.

## Register Audit Table

The implementation agent must use this table as the concrete checklist (in code
comments or a companion note) before writing the masking logic, and the table's
final state must match what STATUS.md reports as Done. Columns: register, used bits
(real state), unused bits (forced to 1), notes. This table is normative for Phase G;
do not leave any row as a research task for the coding agent.

| Register | Address | Used bits | Unused bits → 1 | Notes |
|---|---|---|---|---|
| Sprite X/Y (M0X–M7Y) | $D000–$D00F | all 8 bits each | none | fully used, no masking needed |
| MSB X | $D010 | all 8 bits | none | fully used |
| Control 1 | $D011 | all 8 bits | none | RST8/ECM/BMM/DEN/RSEL/YSCROLL — fully used |
| Raster | $D012 | all 8 bits | none | fully used |
| Light pen X/Y | $D013/$D014 | n/a (Phase F skipped) | n/a | leave as currently stubbed; do not implement latching |
| Sprite enable | $D015 | all 8 bits | none | fully used |
| Control 2 | $D016 | bits 2:0 (XSCROLL), bit 3 (CSEL), bit 4 (MCM) | bits 7:5 | bits 7:5 are unused and read as 1 |
| Sprite Y-expand | $D017 | all 8 bits | none | fully used |
| Memory pointers | $D018 | all 8 bits | none | no Phase G masking; preserve current write/read behavior exactly |
| IRQ status | $D019 | bit 7 (IRQ summary, computed), bits 3:0 (IRQ/IMMC/IMBC/ILP latches) | bits 6:4 | already implemented — add regression test only |
| IRQ enable | $D01A | bits 3:0 (ERST/EMBC/EMMC/ELP enables) | bits 7:4 | already implemented — add regression test only |
| Sprite data priority | $D01B | all 8 bits | none | fully used |
| Sprite multicolor enable | $D01C | all 8 bits | none | fully used |
| Sprite X-expand | $D01D | all 8 bits | none | fully used |
| Sprite-sprite collision | $D01E | all 8 bits, read-clear, write-ignored | none (no "unused" bits, but behavior must be read-clear) | already implemented — add regression test only |
| Sprite-background collision | $D01F | all 8 bits, read-clear, write-ignored | none | already implemented — add regression test only |
| Border color | $D020 | bits 3:0 | bits 7:4 | per Bauer, only low nibble holds color index |
| Background color 0 | $D021 | bits 3:0 | bits 7:4 | same pattern |
| Background color 1 | $D022 | bits 3:0 | bits 7:4 | same pattern |
| Background color 2 | $D023 | bits 3:0 | bits 7:4 | same pattern |
| Background color 3 | $D024 | bits 3:0 | bits 7:4 | same pattern |
| Sprite multicolor 0 | $D025 | bits 3:0 | bits 7:4 | same pattern |
| Sprite multicolor 1 | $D026 | bits 3:0 | bits 7:4 | same pattern |
| Sprite colors 0–7 | $D027–$D02E | bits 3:0 each | bits 7:4 each | same pattern |
| Unused block | $D02F–$D03F | none | all 8 bits | reads return $FF |

The table above is the required Phase G behavior. Do not add masking for any register
not listed with unused bits, and do not force any bits in $D018. If future source
review finds a Bauer/oxyron discrepancy, defer to Bauer and record the discrepancy in
STATUS.md before changing behavior.

## Implementation Approach

- This is a **read-path masking** change. Internal register storage (the byte(s)
  actually written and used for rendering/timing logic) must be unchanged — do not
  reduce storage width or strip bits on write. Only the value returned to the bus on
  read should be adjusted.
- The cleanest place for this is the existing VIC-II register read dispatch (the
  function that currently handles $D019/$D01A masking and $D01E/$D01F read-clear
  per STATUS.md) — extend that same function/table-driven approach rather than adding
  a second, parallel masking mechanism. If the current implementation is a switch/if
  chain per address, follow that same structure for consistency; if it is
  table-driven, add a per-register "unused bit mask" entry to that table.
- For the $D02F–$D03F block: confirm whether reads currently fall through to existing
  mirroring logic and return raw (likely zeroed) storage, or fall through to a default
  case. Wire this range to return `$FF` regardless of what mirroring resolves it to,
  while leaving the mirroring computation itself untouched (per AGENTS.md: do not add
  speculative abstractions, do not refactor adjacent logic that already works).
- Do not add a VIC bus model, a "last fetched byte" tracking field, or any new
  cross-thread state for this phase's required scope. That belongs only to the
  stretch goal below, and must not be attempted unless the human maintainer
  explicitly authorizes it. If separately authorized, it must be machine-owned,
  runtime-published state — per AGENTS.md thread/snapshot rules, no live-machine
  pointers cross threads, and the frontend must only see copied values.

## Stretch Goal (Optional — Do Not Attempt Unless Authorized)

Full last-byte-on-bus tracking: many real C64 boards return the last byte the VIC
fetched during its most recent memory access (c-access, g-access, p-access, s-access,
or idle fetch) when an open VIC address is read, rather than a fixed $FF.

Do not attempt this in the normal Phase G handoff. Only proceed if the human
maintainer explicitly authorizes the stretch goal. If authorized:

- Add a single machine-owned `vic_last_bus_byte` (or equivalently named) field, updated
  at the end of every VIC memory fetch (c/g/p/s-access and idle-state fetches from
  $3FFF/$39FF).
- This field is internal machine state, not part of any existing snapshot struct.
  It must not be read directly by the frontend; if it needs to be surfaced for
  debugging, it must go through the same copied-snapshot publication path as other
  runtime-to-frontend state (per AGENTS.md Snapshot Rule).
- This must not change the required $D02F–$D03F → $FF behavior above as the default;
  it would only apply if the team later decides last-byte behavior should override the
  fixed $FF return, which is explicitly **not** required for this phase.
- If attempted and it does not cleanly fit without touching fetch timing logic
  (Phase A/H territory), abandon it and leave STATUS.md noting it was attempted and
  deferred, rather than partially wiring it in.

This entire section should be skipped in the default Phase G implementation and has no
impact on Phase G completion.

## Files Likely Touched

Based on STATUS.md's description of where prior VIC-II register behavior lives:

- The VIC-II register read function(s) (wherever $D019/$D01A masking and $D01E/$D01F
  read-clear currently live).
- VIC-II register storage/mirroring definitions, only if a per-register mask table is
  added there rather than inline in the read function — no structural change to how
  mirroring itself is computed.
- Regression test file(s) covering VIC-II register read behavior.

Do not touch: rendering (`vicii_live_pixel`, `vicii_make_frame_snapshot`,
`vicii_step_cycle` fetch logic), sprite compositing, IRQ pending callback wiring, or
anything in `runtime`/`frontend`. This phase has no frontend-visible behavior change
beyond what a debugger memory/register view displays when peeking $D000–$D03F.

## Required Regression Test Matrix

Add or extend regression tests so the expected bus-visible read values are explicit.
Use the project's existing VIC-II register test helpers and mirroring helpers rather
than introducing a new test harness.

| Scenario | Setup | Expected read result | Notes |
|---|---|---|---|
| $D016 unused high bits | write $00, then read $D016 | `(read & $E0) == $E0`; `(read & $1F)` still reflects MCM/CSEL/XSCROLL state | repeat with high bits written as both 0 and 1 if helper allows direct register writes |
| $D020-$D02E color high nibbles | for each register, write values such as $00, $0A, $F5 | read returns `$F0 | (written & $0F)` | verifies high nibble forced to 1 while low nibble remains actual color index |
| $D02F-$D03F unused block | write representative values such as $00, $55, $AA, $FF, then read each address | read returns `$FF` | writes must not affect readback |
| Mirrored unused block | read representative mirrors such as $D12F, $D22F, $D32F after the existing mirroring path normalizes them | read returns `$FF` | preserve the existing $D000-$D3FF mirroring computation; do not special-case outside it |
| $D019 IRQ status regression | arrange existing pending/enabled IRQ states using current helpers | bits 6:4 read as 1; bit 7 reflects aggregate enabled-pending IRQ summary | test only; do not reimplement behavior |
| $D01A IRQ enable regression | write representative low-nibble enables | read returns `$F0 | (written & $0F)` | test only; do not reimplement behavior |
| $D01E/$D01F collision regression | arrange or inject existing collision latch values, then read twice and attempt writes | first read returns latch, second read returns cleared latch, writes are ignored | test only; do not reimplement behavior |
| $D018 no Phase G masking | write/read representative values that existing tests can safely exercise | read behavior is unchanged from pre-Phase-G implementation | guards against accidental bit-0 masking |

## Acceptance Criteria

- Reading any address in $D02F–$D03F returns `$FF`, consistent with existing register
  mirroring across the $D000–$D3FF block.
- Reading $D020–$D02E returns bits 7:4 as 1, regardless of prior writes to those bits;
  bits 3:0 continue to reflect the actual written color index.
- Reading $D016 returns bits 7:5 as 1, regardless of prior writes; bits 4:0 continue
  to reflect MCM/CSEL/XSCROLL as before.
- Existing $D019 behavior (bits 6:4 read as 1, bit 7 as aggregate IRQ summary) is
  covered by a passing regression test; no behavior change.
- Existing $D01A behavior (bits 7:4 read as 1) is covered by a passing regression test;
  no behavior change.
- Existing $D01E/$D01F read-clear and write-ignore behavior is covered by a passing
  regression test; no behavior change.
- Internal register storage (the bits actually used by rendering/IRQ/collision logic)
  is bit-for-bit unchanged by this phase — writes still store full bytes; only reads
  are masked.
- All previously passing tests continue to pass.
- Architecture and thread-ownership rules from AGENTS.md remain intact: no new
  live-machine state crosses to the frontend outside the existing snapshot path.

## Definition of Done

- Register audit table above is implemented exactly as the required Phase G behavior.
- All required acceptance criteria pass.
- Stretch goal section is skipped unless the human maintainer explicitly authorized it;
  if authorized, it is either landed cleanly or deliberately abandoned with a STATUS.md
  note — it must not be left half-wired.
- STATUS.md is updated to record:
  - Phase F skipped, Phase G complete.
  - The unused-bit masking behavior added for $D016/$D020–$D02E.
  - The $D02F–$D03F → $FF behavior added.
  - Confirmation that $D019/$D01A/$D01E/$D01F behavior was re-verified by regression
    test, not reimplemented.
  - Whether the last-byte-on-bus stretch goal was attempted, and its outcome.
