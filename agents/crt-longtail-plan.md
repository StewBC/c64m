# CRT long-tail mappers — types 15, 7, 8, 17

Spec + implementation record for the four cheap OneLoad64 cartridge mappers that
follow Magic Desk (19) / Ocean (5). Companion to `crt-type19-plan.md`. Source of
truth is the C code; keep this current when behavior changes.

Oracle: VICE `src/c64/cart/{gs,funplay,supergames,dinamic}.c`
(`/Users/swessels/Develop/svm/vice-emu-code/vice`). Hardware outranks VICE if
they ever disagree; document any deliberate difference.

## Scope

| Type | Short name | OneLoad64 v5 count | Banks | Window | Register |
|-----:|------------|-------------------:|-------|--------|----------|
| 15 | C64GS / System 3 | 4 | 64 × 8K | ROML `$8000` (8K game) | IO1 read **or** write, bank = `addr & $3f` |
| 7 | Fun Play / Power Play | 2 | 16 × 8K | ROML `$8000` (8K game) | IO1 **write**, bank scrambled; `$86` = ROM off |
| 8 | Super Games | 2 | 4 × 16K | ROML+ROMH `$8000–$BFFF` (16K) | **IO2** `$DF00` write, bank=`v&3`, mode+latch |
| 17 | Dinamic | 5 | 16 × 8K | ROML `$8000` (8K game) | IO1 **read** of `$DE00–$DE0F`, bank = `addr & $0f` |

Total 13 files. Zaxxon (18) and GMod2 (60) are **out of this milestone** (ROML-read
side-effect and serial-EEPROM save respectively — different mechanisms).

Sample titles for live smoke (2 per type) under
`/Volumes/EXTERNAL/Temp/OneLoad64-Games-Collection-v5/Extras/`:

- **15:** `OfficialCRTs/Last Ninja Remix.crt`, `OfficialCRTs/Myth - History in the Making.crt`
- **7:** `OfficialCRTs/Funplay (Codemasters ...).crt`, `OfficialCRTs/Power Play 64 (...).crt`
- **8:** `OfficialCRTs/Super Games (...).crt`, `OtherCRTs/Vegetables Deluxe.crt`
- **17:** `OfficialCRTs/Narco Police.crt`, `OfficialCRTs/Satan.crt`

## Hardware models (locked against VICE)

### Type 15 — C64GS / System 3 (`gs.c`)
- 64 × 8K ROML at `$8000`, permanent 8K game (EXROM low, GAME high). No disable bit.
- On **any** IO1 access (read or write) in `$DE00–$DEFF`: `bank = addr & $3f`
  (address bits, value ignored). Read returns 0.
- CRT: chips ROM, `size=$2000`, `load=$8000`, `bank ≤ 63`, linear.
- Power-on / plain reset: bank 0.

### Type 7 — Fun Play / Power Play (`funplay.c`)
- 16 × 8K ROML at `$8000`, 128K.
- IO1 **write** (`$DE00–$DEFF`, address ignored): `bank = ((v>>3)&7) | ((v&1)<<3)`.
- Mode from value: `(v & $c6)==$00` → 8K game; `(v & $c6)==$86` → ROM off
  (EXROM+GAME both inactive → RAM at `$8000`). Other values: VICE warns, we treat
  as unchanged (keep last good config).
- **CAUTION:** CRT `chip.bank` is the *register value* (scrambled), not linear.
  Both load and latch de-scramble. Header lines are `exrom=0 game=0` (16K flag) but
  the mapper runs 8K — predicate keys on chip layout, not header.
- Power-on / plain reset: latch 0 → bank 0, 8K game.

### Type 8 — Super Games (`supergames.c`)
- 4 × 16K banks at `$8000–$BFFF` (8K ROML + 8K ROMH per bank), 64K.
- Control at **IO2** `$DF00–$DFFF` (write-only):
  - bits 0–1: bank
  - bit 2: mode — `0` = 16K game (EXROM+GAME both active), `1` = cart disabled (RAM)
  - bit 3: **write-protect latch** — once set, register frozen until hardware reset
- CRT: chips ROM, `size=$4000`, `load=$8000`, `bank ≤ 3`.
- Power-on / plain reset: latch cleared, register 0 → bank 0, 16K enabled.

### Type 17 — Dinamic (`dinamic.c`)
- 16 × 8K ROML at `$8000`, permanent 8K game.
- IO1 **read** only: reading offset `$00–$0F` (`$DE00–$DE0F`) sets `bank = addr & $0f`.
  Reads of `$DE10–$DEFF` and all writes do nothing. Read returns 0.
- CRT: chips ROM, `size=$2000`, `load=$8000`, `bank ≤ 15`, linear.
- Power-on / plain reset: bank 0.

## c64m mapping decisions

The existing bus cart model is a single 8 KiB-bank ROML heap
(`cartridge_rom_banks`, `bank_count` × `$2000`) plus one ROML and one ROMH window
cache, an `io_latch`, `bank_mask`, `hardware_type`, and `exrom/game/mode` lines.
c64m convention: **0 = line active (asserted low)**, matching the CRT header.

- **15 / 17 / 7** are single-8K-bank ROML mappers → fit the heap directly.
  - 15, 17: `io_latch` holds the *selected bank number* (set from the accessed
    address). Reuse the generic linear multibank copy.
  - 7: `io_latch` holds the *register value*; latch de-scrambles it. Load path
    de-scrambles `chip.bank → linear` before storing.
- **8 (Super Games)** needs a distinct ROML **and** ROMH per bank, which the heap
  does not carry (Ocean 16K only *mirrors* one bank). **Decision:** store Super
  Games as `2 × N` interleaved 8 KiB slots — `[b0_ROML, b0_ROMH, b1_ROML, …]` —
  so `bank_count = 2·N` in the same 8K-bank heap. Latch selects
  `ROML = slot[2·bank]`, `ROMH = slot[2·bank+1]`. No new storage field, no
  snapshot layout change.

### Snapshot
**No format bump (stays v13).** Every new type is fully described by the fields
already serialized (`hardware_type`, `bank_count`, `bank_mask`, `io_latch`,
ROML blob, ROMH window/present). Restore calls
`c64_bus_cartridge_apply_banking`, which recomputes all windows/lines/mode
(including Super Games `bank`/`mode`/`write-protect` from `io_latch` bit 3) from
`io_latch` + `hardware_type`. Old snapshots are unaffected (unknown types were
never written).

## Touch list

| File | Change |
|------|--------|
| `src/tools/crt/crt.c` + `.h` | 4 type constants, max-bank constants, 4 `_supported` predicates, add to `crt_image_is_supported` |
| `src/machine/c64_bus.h` | 4 `HW_*` constants |
| `src/machine/c64_bus.c` | 4 apply-latch fns; IO1 read side-effect (15, 17) in `c64_io_read`; IO1 write (15, 7) + IO2 write (8) in `c64_io_write`; 4 attach fns; reset + apply_banking dispatch |
| `src/machine/c64.c` + `.h` | 4 `c64_attach_*` wrappers |
| `src/runtime/runtime_thread.c` | Fun Play de-scramble copy, Super Games 16K-split copy, 4 attach helpers, load dispatch |
| tests | unit (parse/bank/mode/disable per type) + regression; live smoke |
| docs | `machine.md` cartridge section, `crt-type19-plan.md` checklist marks, this file |

## Verification

```text
ctest --test-dir build --output-on-failure
./build/c64m --crt "<sample>.crt"   # 2 per type, windowed, no --headless
```

## Status

| Item | State |
|------|-------|
| Spec locked against VICE | yes |
| Blocking issues found | none (Super Games ROMH via 2×8K interleave; no snapshot bump) |
| Implementation | **done** — parse + bus + attach + load + reset/apply + tests |
| Unit tests | `test_crt` (predicates + de-scramble), `test_c64_bus` (bank/mode/disable/latch per type) |
| Runtime tests | `test_runtime_crt` — load + bank-switch (C64GS write, Fun Play de-scramble, Super Games 16K split); Dinamic load-only (read-switch needs a real CPU read, covered by `test_c64_bus`) |
| Full ctest | 60/60 green |
| Live smoke | 2 real OneLoad64 titles per type booted headless to title/menu screens (15, 7, 8, 17 all OK) |

Note: the debug memory inspector (`c64_debug_read_cpu_map` → `c64_debug_peek_io`) is
side-effect-free, so a control-port memory read cannot drive read-latched mappers
(Dinamic bank switch, C64GS read-switch). Drive those via a CPU read or a write; the
bus unit tests exercise the read side-effect through `c64_bus_read` directly.
