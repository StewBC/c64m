# CRT mappers plan — Type 19 (Magic Desk) first

Implementation plan for expandable cartridge-mapper support. **Do not treat this
as done work** until the type checklist below is updated and the source matches.

Related handoffs: `machine.md` (cart attach / bus), `disk-iec1541.md` (CRT is not
disk I/O), `tools.md` (CRT parser), `testing.md`. Source of truth for behavior is
always the C code; keep this doc current when mapper support lands.

## Goal (this milestone)

Add **CRT hardware type 19 — Magic Desk / Domark / HES** so multi-bank 8K game
cartridges load and bank-switch correctly.

Non-goals for this milestone:

- Full VICE cart parity
- Freeze carts (Action Replay, FC3, Super Snapshot, …)
- EasyFlash flash semantics (type 32) — track separately after 19
- REU / GeoRAM (expansion RAM + DMA; **not** CRT game types)
- Cycle-perfect IO1 timing beyond “write selects bank, ROML reads that bank”

Design for **later mappers** without implementing them yet.

## Why type 19 first

OneLoad64 Games Collection v5 census (recursive `.crt` only; 2829 files):

| Rank | Type | Share (approx.) |
|------|------|-----------------|
| 1 | **19 Magic Desk** | **2171 (~77%)** |
| 2 | 32 EasyFlash | 385 (~14%) |
| 3 | 0 Normal | 234 (~8%) |
| rest | 5, 7, 8, 15, 17, 18, 60 | tiny |

c64m already accepts **type 0** (generic 8K/16K). Adding **type 19** unlocks nearly
the entire OneLoad root set and every file currently under `assets/crt/`.

Census root path used for the counts below (re-run if the collection moves):

`/Volumes/EXTERNAL/Temp/OneLoad64-Games-Collection-v5`

## Type checklist (OneLoad64 v5)

Mark `[X]` only when c64m can **attach and run** that type for typical dumps of
that ID (not merely parse the header). Counts are **how many `.crt` files of that
type exist in the collection** (unlock potential), not a guarantee every title
plays after the mapper lands.

| Done | Type | Short name | Count (OneLoad64 v5) |
|------|-----:|------------|---------------------:|
| [X] | 0 | Normal (generic 8K/16K) | 234 |
| [X] | 5 | Ocean type 1 | 22 |
| [ ] | 7 | Fun Play / Power Play | 2 |
| [ ] | 8 | Super Games | 2 |
| [ ] | 15 | C64 Game System / System 3 | 4 |
| [ ] | 17 | Dinamic | 5 |
| [ ] | 18 | Zaxxon | 2 |
| [X] | **19** | **Magic Desk / Domark / HES** | **2171** |
| [ ] | 32 | EasyFlash | 385 |
| [ ] | 60 | GMod2 | 2 |

**Totals:** 2829 `.crt` · 10 distinct hardware types · 0 invalid headers in census.

Folder mix (for smoke-test path picks):

| Folder (rel.) | Dominant types |
|---------------|----------------|
| `(root)` | 19 × 2145 |
| `Extras/OfficialCRTs` | 0 × 190, then 5/17/… |
| `Extras/OtherCRTs` | 32 × 196, 0 × 43, 19 × 12 |
| `MultiLoad64` | 32 × 140, 19 × 9 |
| `AlternativeFormats/EasyFlash` | 32 × 49 |
| `Dumps/SourceFiles` | 0 × 1 |

Types **not** present in that census are out of this checklist until something
needs them. REU is intentionally absent (not a CRT hardware_type for game dumps).

## Current implementation (baseline)

| Layer | Location | Behavior |
|-------|----------|----------|
| Parse | `src/tools/crt/crt.c` | Full header + CHIP parse; `crt_image_is_generic_supported`, `crt_image_is_magic_desk_supported`, `crt_image_is_supported` |
| Load | `runtime_load_crt()` in `runtime_thread.c` | Type 0 / 5 Ocean / 19 Magic Desk attach |
| Attach | `c64_attach_generic_cartridge` / ocean / magic_desk | Type 0: one ROML ± ROMH; 5/19: N×8K banks |
| Bus | `c64_bus` | Multi-bank ROML heap; IO1 `$DE00–$DEFF` for Ocean and Magic Desk |
| Snapshot | `c64_snapshot` v13 | Hardware type, bank count/mask/latch, multi-bank ROM, optional ROMH |

Type 0 status notes:

- Supported: Normal 8K (EXROM=0, GAME=1) and 16K (EXROM=0, GAME=0) with bank-0 ROM chips only.
- Not yet: Ultimax attach (mode enum exists; generic attach rejects Ultimax), odd CHIP layouts still tagged type 0.

## Magic Desk (type 19) — hardware model (locked)

Aligned with VICE `magicdesk.c` / cart manual:

- **CRT ID:** 19  
- **Memory:** 8K game — ROML at `$8000–$9FFF` (EXROM low, GAME high)  
- **Banks:** Multiple 8K ROM chips, typically load `$8000` (VICE also accepts `$A000`), bank index 0..N−1, max **128** banks  
- **Bank select:** Write to **IO1** `$DE00–$DEFF` (mirrors; address ignored)  
  - bits 0–6: bank number, masked by `bankmask` from highest bank index  
  - bit 7: cart disabled → no ROML, RAM at `$8000`  
- **bankmask** (from last bank index): ≥64→`$7F`, ≥32→`$3F`, ≥16→`$1F`, ≥8→`$0F`, ≥4→`$07`, else `$03`  
- **IO1 read:** write-only (open bus / `$FF`)  
- **Power-on / plain reset:** latch `$00` (bank 0, enabled)  
- **No freeze**, no on-cart RAM for common game dumps  

Typical OneLoad dump shape (e.g. `assets/crt/Cybernoid.crt`):

- Header type `0x0013` (19), EXROM=0, GAME=1  
- 10× CHIP ROM, banks 0–9, each 8K @ `$8000` → bankmask `$0F`

## Architecture (do once, use for 19+)

```text
tools/crt          — parse + support predicates
machine bus cart   — multi-bank storage, IO1 dispatch, mapper id
runtime            — load file → parse → attach mapper → reset
```

Type 0 and type 19 share heap multi-bank storage + ROML window; type 0 is a single bank with no IO1 effect.

## Implementation steps (type 19)

### 1. Spec lock

- [X] Confirm bank register bits and IO1 range against VICE Magic Desk implementation.  
- [X] Note max banks: 128 (VICE); in-tree dumps 10–12 banks.  
- [X] Document EXROM/GAME expectations (above).

### 2. Cart state + bus plumbing

- [X] Multi-bank ROML storage + active bank window.  
- [X] Wire IO1 writes to Magic Desk latch.  
- [X] Cart read path uses active ROML window.  
- [X] Detach clears banks and bank register.  
- [X] Plain reset keeps cart; Magic Desk re-selects bank 0 / enabled.

### 3. CRT load path

- [X] Accept hardware_type 19 when CHIP list is ROM, 8K @ `$8000`/`$A000`.  
- [X] Reject garbage layouts with clear error (includes type id).  
- [X] Keep type 0 path green.  
- [X] Runtime: parse → validate → copy banks → attach → reset → run.

### 4. Snapshot

- [X] Save/load multi-bank ROM, active latch, mapper id, lines (format **v13**).  
- [X] Bump format version; update `machine.md` / snapshot notes.

### 5. Tests

- [X] Unit: parse synthetic type-19; `supported == true`.  
- [X] Unit: IO1 write changes bytes visible at `$8000`.  
- [X] Unit: bank mask / disable bit 7.  
- [X] Runtime: synthetic Magic Desk load without unsupported-type error.  
- [ ] Optional smoke: live `assets/crt` title (manual / agent live run).  
- [X] Regression: existing type-0 CRT tests and bus cart tests (full ctest green).

### 6. Docs

- [X] Update `machine.md` cartridge section (type 0 + 19).  
- [X] Update `disk-iec1541.md` CRT bullet.  
- [X] Update `architecture.md` product boundary.  
- [X] Mark type 19 `[X]` in the checklist above.  
- [ ] Manual only if user-facing CRT help claims change (`manual/` via HELP rules) — no claim change required yet.

## Suggested later order (after 19)

Driven by OneLoad counts, not by CRT ID number:

1. **32 EasyFlash** — 385 files; multi-load / modern dumps. Larger than Magic Desk.  
2. Long tail: 17, 15, 7, 8, 18, 60 — only if a title you care about needs them.
   (Ocean type 5 is done.)

Each new type: add mapper ops, validation, tests, checklist `[X]`, census count stays
as historical unlock size unless re-tallied.

## Out of scope reminders

| Topic | Note |
|-------|------|
| REU (1750/1764) | Expansion RAM + DMA; separate machine feature, not CRT type 19 |
| GeoRAM / NeoRAM | Windowed RAM; separate |
| Freezers | NMI / UI freeze button / complex banking |
| “All VICE types” | Multi-year; not this plan |

## Verification

```text
ctest --test-dir build --output-on-failure
./build/c64m --crt assets/crt/Cybernoid.crt
```

## Oracle

- VICE `magicdesk.c` (bankmask, IO1, bit 7 disable, max 128 banks).  
- Hardware outranks VICE if they disagree; document any deliberate difference.

## Status

| Item | State |
|------|--------|
| Plan written | yes |
| Type 19 implementation | **done** (attach + bank + load + snapshot + tests) |
| Type 0 | done (generic 8K/16K only) |
| Live title smoke | run when verifying (windowed, no `--headless`) |
