# Known gaps

What the product does not claim, and work that is explicitly out. Check source
before treating a row as still missing. Do not reopen the closed decisions at
the bottom.

## Emulation

| Gap | Notes |
|-----|-------|
| EasyFlash (CRT type 32) | Types 0, 5, 7, 8, 15, 17, 19 are implemented. Freeze carts are out. |
| REU / GeoRAM | Expansion RAM + DMA; not a CRT game type. |
| Ultimax attach | Decode exists; generic attach rejects Ultimax. |
| SID 8580 / analog blend / paddles | 6581-style functional model. `$D419/$D41A` read `$FF`. No runtime chip switch. Combined-wave is a deterministic approximation. |
| Light pen `$D013/$D014` | Stub: register store only. |
| Half-cycle / analog AEC, RDY, IEC, CIA serial | Cycle granularity. General last-byte-on-bus is not modeled; BA-lead VIC cbuf (`ram[PC] & 0x0f`) is. |
| Unstable undocumented opcodes | XAA, AHX, SHX, SHY, TAS, LAS, LAX #imm, JAM use the compatibility executor. No Harte corpus in ctest. |
| Datasette / TAP bitstream | T64 is host convenience: first loadable PRG extract. No mounted tape. |
| CIA tape, RS-232, user port | FLAG/SP/PC exist on the chip; no peripherals. |
| 1541: devices 10+, 1571, cross-drive copy | Devices 8 and 9 only. |
| G64 empty-track grow / format rebuild | Length-preserving in-place write-back only. |
| Broad fast-loader matrix | Arkanoid V-MAX and Robocop G64 are validated; that is not commercial coverage. |
| VIA shift register | Ports + T1/T2 + CA1. No SR/CB2 stepping. |
| Snapshot extras | `vic_irq_delay`, CIA timer delay bits, VIC paint pipes, and SID clock tables are not in `.c64state`. Paint buffers are display cache: zeroed on load. Optional drive-ROM FNV hash is a source TODO. |

## Inspector and debug

| Gap | Notes |
|-----|-------|
| Promote / Branch | Out. Inspector lands the past into the one true `c64_t` and leave restores NOW. Do not make the past become a new live line. |
| TimeMachine names | Product is Inspector. Identifiers are `runtime_inspector_*`. |
| Drive CPU in HST1 | Main 6510 only. |
| Memory fill / move / named range diff | Debugger has get/set memory and HST1. Classic monitor fill/move/backup-diff is not a product verb. |
| Second control client | One TCP client. Co-op is windowed UI + one script (`using-c64m.md`). |

Inspector itself is shipped (opt-in `--inspector` / `[debug] inspector`, Misc
Inspector tab, `enter-inspector` / `leave-inspector`). Forensics (Opt+R /
Inspector **Forensics...**) is the in-emulator HST1 FIND transcript (see the
manual **Forensics** section). File snapshots (`save-state` / `load-state`)
are a different product.

## Tests and oracles

- Ten asset-gated tests SKIP when `assets/` is missing (`testing.md`). That is
  not a gap in the product; it is a checkout fact.
- CIA race work uses `tools/cia-timing-corpus/` (fetch into
  `external/cia-timing-corpus/`, gitignored). It is evidence, not a ctest gate.
- Edge of Disgrace checker vs VICE is a manual oracle, not a unit test.
- Most Nuklear dialogs are manual smoke.

## Do not reopen

These were decided with measurements. A new brief needs new evidence.

- Inspector is checkpoint ring + input log + sealed re-execute, not an HST1
  walk and not a write-delta stream.
- Join film to checkpoints by `machine_cycle`, not frame number. Missing film
  is pink; do not invent stills.
- One debugger skin. Inspect is a mode of F9, not a second shell. Host F7 stays
  unbound (C64 F7 is a guest key).
- One breakpoint list. Inspect F12 stops on that list or at live.
- PAL 32/320/32 is a frontend crop from VIC X 496, not a modular origin shift
  of the framebuffer.
- Over-border graphics data is zero, not the `$3FFF` ghost byte.
- `xscroll_pipe` samples `$D016` only on g-access cycles 15..54.
- EOF resets only `VC`/`VCBASE`; `RC`, `VMLI`, and display state carry.
- Unpowered 1541s must not sit on IEC (ATN acknowledge clamps DATA).
- Media-on EXECUTE is not synthetic `format_track()`.
- Warp (turbo 3) is the only paint-off path. Max (turbo 2) is the correctness
  and throughput bar.
- Wire identity is `C64M/8` with no dual-path compatibility layer. Bump `N`
  in the same change as the code and `control-port.md`.
