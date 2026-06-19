# STATUS.md

## Current State

Completed through Phase 16 (timed bus events and live VIC-II raster) and assembler UI integration (Phase 16 of ASMDESIGN.md), VIC-II Phase E (sprite priority and collisions), VIC-II Phase G (open bus / unused register reads), VIC-II Phase H (sprite-fetch BA cycle stealing), VIC-II Phase J (DEN-off visual blanking), CIA Phase A (register map, mirroring, safe reads, and current-state reconciliation), CIA Phase B (Timer A/B core countdown and reload hardening), CIA Phase C (timer control modes, PB output, and cascade sources), CIA Phase D (interrupt control register and IRQ/NMI line behavior), CIA Phase E (CIA #1 keyboard, joystick, and RESTORE port integration), CIA Phase F (CIA #2 VIC bank and IEC port integration), and CIA Phase G (time-of-day clock and alarm). VIC-II Phase F (light pen) is skipped.

D64 disk support Phase A is complete as a tools-only parser:

- `src/tools/d64` parses standard 35-track D64 sector geometry and accepts the
  common 35-track error-info size by ignoring the appended per-sector error bytes.
- BAM metadata parsing exposes disk title, disk ID, DOS type, and free-block count.
- Directory enumeration follows the track 18 sector 1 directory chain, lists ordinary
  visible entries, preserves raw PETSCII filename bytes, and exposes ASCII debug names.
- PRG extraction follows file track/sector chains and returns the full PRG byte stream,
  including the two-byte little-endian load address.
- Regression coverage includes `assets/disks/blank.d64`, `assets/disks/ODELLLAK.D64`,
  generated minimal PRG images, malformed directory pointers, directory loops, file
  chain loops, out-of-range file sectors, unsupported SEQ extraction, and too-short PRGs.
- Runtime mount state, KERNAL LOAD traps, `LOAD "$",8` directory synthesis, disk writes,
  1541 CPU/ROM emulation, IEC timing, fast loaders, 40/42-track variants, and full
  error-info semantics remain not implemented.

D64 disk support Phase B is complete for device 8 mount plumbing:

- Runtime commands can mount a D64 path, unmount device 8, and request copied disk
  status.
- Runtime reads the host file and validates it with the tools-level D64 parser on the
  runtime thread.
- The live mounted state is machine-owned: device slots currently cover devices 8 and 9
  structurally, with Phase B behavior implemented for device 8.
- Machine drive state stores copied standard 35-track D64 bytes plus copied display
  metadata; frontend/runtime events expose copied status only and no live pointers.
- Replacing a valid mounted disk frees/replaces the previous owned image; failed mount
  attempts report an error status while preserving the previous successful mount.
- Regression coverage includes runtime mount of `blank.d64`, replacement with
  `ODELLLAK.D64`, copied display name/title status, missing-file mount failure, and
  unmount clearing copied status.
- KERNAL LOAD traps, directory loads, PRG loads from D64, device 9 behavior, D64 writes,
  1541 CPU/ROM emulation, IEC timing, and fast loaders remain not implemented.

D64 disk support Phase C is complete for mounted device 8 PRG loads:

- Machine-owned KERNAL LOAD trap handles PC `$FFD5` for device 8 only; other devices
  fall through to ROM behavior.
- `LOAD "NAME",8,1` loads PRG payload bytes at the embedded two-byte PRG load address
  and returns carry clear with X/Y containing the final exclusive end address.
- `LOAD "NAME",8` loads PRG payload bytes at the current BASIC start pointer
  (`$2B/$2C`) and updates BASIC end pointers (`$2D/$2E`, `$2F/$30`, `$31/$32`) plus
  the KERNAL end address pointer (`$AE/$AF`).
- Exact filename matching supports ordinary C64/PETSCII names, with surrounding quotes
  ignored and ASCII case folded for normal names.
- Failure paths return carry set without corrupting unrelated memory for no mounted
  disk, missing files, unsupported file types such as SEQ, malformed chains, oversized
  target ranges, and unsupported load/verify modes.
- Regression coverage includes `LOAD "MENU1",8,1`, `LOAD "MENU1",8`, missing-file
  sentinel preservation, SEQ rejection for `LAKESTR.TXT`, device 9 fallthrough, and
  no-disk failure.
- `LOAD "$",8`, wildcard matching, device 9 disk behavior, D64 writes, error channel,
  1541 CPU/ROM emulation, IEC timing, and fast loaders remain not implemented.

D64 disk support Phase D is complete for `LOAD "$",8` directory loading:

- KERNAL LOAD trap recognizes filename `$` on mounted device 8 and synthesizes a valid
  tokenized BASIC directory program at the current BASIC start pointer.
- Directory output uses stable first-pass formatting: line 0 contains disk title/ID/DOS
  type, each visible entry shows block count, quoted filename, and file type, and the
  final line reports blocks free.
- BASIC line links are absolute C64 addresses and terminate with `$0000`, so `LIST`
  can traverse the generated program.
- BASIC end pointers (`$2D/$2E`, `$2F/$30`, `$31/$32`) and KERNAL end address pointer
  (`$AE/$AF`) are updated consistently with the generated program end.
- Regression coverage includes `blank.d64` directory load, `ODELLLAK.D64` directory
  load, expected names (`MENU1`, `LAKESPT.BIN`, `LAKESTR.TXT`), file type text such as
  `SEQ`, valid BASIC line links, end-pointer updates, no-disk failure, and existing
  Phase C PRG load behavior.
- Directory pattern filters, wildcard PRG matching, device 9 disk behavior, D64 writes,
  error channel, 1541 CPU/ROM emulation, IEC timing, and fast loaders remain not
  implemented.

D64 disk support Phase E is complete for filename matching, wildcard loads, and tighter
load failures:

- Filename matching preserves raw mounted directory bytes and normalizes only ordinary
  ASCII letter case for comparison; punctuation, digits, and spaces remain literal.
- Surrounding quote characters in the KERNAL filename buffer are ignored for matching.
- PRG loads support exact names, `*` for any suffix, prefix wildcards such as `LAKE*`,
  and `?` as a single-character wildcard.
- `LOAD "*",8` and `LOAD "*",8,1` select the first visible PRG in mounted directory
  order; wildcard PRG loads skip unsupported file types such as SEQ.
- Directory load `$` remains special and is not treated as a filename wildcard.
- Failure paths are deterministic and preserve unrelated memory for no disk, missing
  exact names, missing wildcard matches, unsupported file types, chain loops,
  out-of-range sectors, target overflow, and unsupported load modes.
- Regression coverage includes lowercase exact matching, `*`, `LAKE*`, `MENU?`,
  wildcard SEQ skipping, missing wildcard failure, missing exact failure, SEQ rejection,
  generated loop/out-of-range mounted images, and continued `$` directory load behavior.
- Full Commodore DOS pattern semantics, file type suffix parsing, device 9 disk
  behavior, D64 writes, error channel, 1541 CPU/ROM emulation, IEC timing, and fast
  loaders remain not implemented.

D64 disk support Phase F is complete for Machine-tab disk UI/status and validation:

- Machine tab now has a compact Disks section with device 8 mount and unmount controls.
- Device 8 mount opens the host file picker with broad/all-file selection and sends a
  copied runtime mount command; unmount sends a copied runtime unmount command.
- Frontend displays copied runtime disk status only, preferring disk title, then host
  basename/status text.
- Device 9 is displayed as deferred and does not imply working disk behavior.
- Runtime disk status is requested with normal debugger refresh and updated from copied
  `RUNTIME_EVENT_DISK_STATUS_RESPONSE` events.
- Automated validation covers parser, mount/unmount status, KERNAL PRG load,
  `LOAD "$",8` directory load, wildcard/error behavior, blank and ODELL fixtures, and
  malformed mounted chains. Manual GUI validation remains the next human smoke step for
  selecting a D64 through the native picker and typing BASIC commands.
- Host load/save UI, D64 writes, SAVE to disk, device 9 disk behavior, error channel,
  1541 CPU/ROM emulation, IEC timing, and fast loaders remain not implemented.

D64 disk support Phase G is complete for optional device 9 support:

- Devices 8 and 9 can independently mount/unmount read-only D64 images through runtime
  commands and Machine-tab UI controls.
- KERNAL LOAD trap supports `LOAD "$",9`, `LOAD "NAME",9`, and `LOAD "NAME",9,1`
  with the same directory, PRG, wildcard, and failure behavior as device 8.
- Copied status events and frontend labels update independently for device 8 and 9.
- Device 8 and device 9 can hold different mounted images; unmounting one does not
  disturb the other.
- Regression coverage includes mounting `ODELLLAK.D64` as device 9, device 9 directory
  load, device 9 `MENU1` PRG load, independent device 8/9 status, device 9 unmount
  preserving device 8, missing device 9 disk failure, and unsupported device 10
  fallthrough.
- Device numbers beyond 8 and 9, D64 writes, SAVE to disk, error channel,
  1541 CPU/ROM emulation, IEC timing, and fast loaders remain not implemented.

Implemented:

- 6510 CPU integrated through C64 bus.
- RAM, ROMs, banking, and address decoding.
- ROM loading and reset-vector boot path.
- Runtime thread and command/event model.
- Run, pause, reset, cycle-step, instruction-step.
- Frame pipeline with copied runtime-to-frontend handoff.
- VIC-II skeleton:
  - register storage
  - register mirroring
  - raster timing foundation
  - frame generation
  - border/background rendering
- Character display bring-up:
  - screen RAM fetch from $0400
  - character ROM glyph fetch
  - color RAM nibble storage and fetch
  - 40x25 text rendering into the active display area
- VIC-II review/implementation state after the Phase A/B/C planning review:
  - `$D019` IRQ status reads now model bit 7 as the enabled-pending VIC IRQ summary;
    bits 6:4 read as 1, and `$D01A` still reads with the high nibble set
  - raster IRQ status/enable is wired into the CPU IRQ pending callback
  - Bad Line detection exists for DEN-enabled visible lines matching YSCROLL
  - Bad Line c-access fetches populate the internal 40-byte video matrix/color line
    buffers during cycles 15-54
  - BA is asserted at cycle 12 on Bad Lines and released after the c-access window;
    this is still an approximation of RDY/AEC because CPU read-vs-write cycle
    discrimination is not implemented
  - VC/VCBASE/RC/display-state bookkeeping exists for Bad Line/text-row progression
  - PAL/NTSC raster line and frame counts are selected from machine configuration
  - snapshot rendering now applies PAL border-window geometry with RSEL/CSEL:
    25/24 row vertical clamps and 40/38 column horizontal clamps
  - snapshot rendering applies XSCROLL/YSCROLL as delayed display-window edges:
    pixels before the scroll offset show background, then content begins from row/col 0
  - snapshot rendering supports the VIC-II graphics-mode dispatch for ECM/BMM/MCM:
    standard text, multicolor text, standard bitmap, multicolor bitmap, ECM text,
    and invalid modes 5/6/7 as black background/display-layer pixels
  - standard bitmap mode uses bitmap data from `$D018` bit 3 and colors from the
    video matrix byte; color RAM is not used for standard bitmap colors
  - multicolor bitmap and multicolor text color selection paths are implemented in
    the snapshot renderer
  - invalid graphics modes black the background/display layer while leaving border
    rendering unaffected
  - mode/scroll changes are reflected on the next snapshot render; per-cycle
    mid-frame mode switching is not implemented
  - frontend display presentation crops the internal 384x272 frame to a balanced
    352x240 view for both display-only and debugger-pane rendering, leaving the
    internal frame/raster geometry unchanged
- CIA foundations:
  - CIA #1 and CIA #2 machine-owned devices
  - `$DC00-$DCFF` and `$DD00-$DDFF` bus routing
  - register storage and `addr & $0F` mirroring across the full 256-byte CIA pages
  - timer A/B latch, counter, and underflow foundations
  - interrupt mask/flag foundations
  - CIA #1 IRQ pending callback path
  - CIA #2 NMI pending foundation
  - deterministic no-key keyboard matrix reads
- CIA Phase A register/safe-read reconciliation:
  - CPU-visible CIA reads use normal side-effecting register semantics through
    `cia_read_register()`
  - debugger and memory snapshot reads use `cia_debug_read_register()` through
    `c64_debug_read_cpu_map()`, avoiding ICR clear-on-read and preserving future TOD
    latch side-effect boundaries
  - Timer A reads at `$04/$05` and Timer B reads at `$06/$07` return live counters for
    both normal CPU reads and debugger-safe peeks
  - Timer A/B writes through `$04/$05` and `$06/$07` update latches; while stopped,
    the current deterministic behavior immediately loads the live counter
  - the observed `$DC06 == $FF` Timer B concern was reconciled as a manual diagnostic
    expectation issue: the counter was advancing too quickly for a `$FE` poll to catch
  - `c64_step_cycle()` no longer performs side-effecting CPU reads while preparing a
    deferred instruction trace; trace discovery uses debugger-safe peeks and replays the
    real `c64_bus_read()` at the recorded bus event cycle
  - `c64_bus_vic_bank_base()` reads CIA #2 port A pins via a non-side-effecting
    `cia_read_port_a_pins()` helper instead of the CPU-visible CIA read API
  - regression coverage includes CIA page routing/mirroring, live Timer B/debug peeks,
    debugger-safe ICR reads, VIC-bank pin reads, and a cycle-stepped `$DC0D` ICR
    clear-on-read timing test
- CIA Phase B timer hardening:
  - Timer A and Timer B are treated as 16-bit down-counters clocked once per current
    emulator system cycle when running in Phi2 mode; this is the current project
    abstraction, not sub-cycle/pin-level CIA timing
  - latch and live counter state remain separate: timer register writes update the
    latch, and timer reads return the live counter
  - stopped timer-register writes immediately load the live counter from the effective
    latch; the project policy for latch `$0000` is to load/reload `$FFFF`
  - CRA/CRB bit 4 is a force-load strobe: writing it copies the effective latch into
    the live counter, but the bit is not retained in the stored control register
  - continuous timers underflow, set the Timer A/B ICR source flag, reload from the
    effective latch, and keep running
  - one-shot timers underflow, set the Timer A/B ICR source flag, reload from the
    effective latch, and clear the start bit
  - CPU-visible Timer B programming through `$DC06/$DC07/$DC0F` has a regression
    diagnostic proving a later `$DC06` read observes a decreased live counter
  - regression coverage now includes stopped timers, force-load strobe behavior,
    continuous reload, one-shot reload/stop, zero-latch effective loading, timer source
    flags, and the CPU-visible Timer B countdown path
  - Phase B deliberately left CNT input, full Timer B cascade behavior, PB6/PB7 output,
    pin-level edge races, and hardware-variant timing to later phases
- CIA Phase C timer control modes and PB outputs:
  - CRA bit 5 selects Timer A source: Phi2 when clear, CNT pulse events when set
  - CRB bits 5-6 select Timer B source: Phi2, CNT pulse events, Timer A underflows, or
    Timer A underflows coincident with a CNT pulse
  - generic CIA CNT input is represented by `cia_pulse_cnt()`, which schedules one CNT
    event consumed by the next `cia_step_cycle()`
  - Timer A underflow state is available within the same CIA step for Timer B cascade
    and combined cascade+CNT modes
  - CRA bit 1 enables Timer A output on PB6; CRB bit 1 enables Timer B output on PB7
  - CRA/CRB bit 2 selects toggle output when set; when clear, underflow emits a
    deterministic one-CIA-step low pulse and then restores high
  - PB6/PB7 timer outputs override ordinary Port B read bits only while their timer
    output-enable bit is set; with output disabled, ordinary PRB/DDR behavior is
    preserved
  - regression coverage includes Timer A CNT, Timer B Phi2/CNT/cascade/combined
    sources, PB6/PB7 pulse and toggle outputs, ordinary Port B behavior with timer
    output disabled, plus keyboard/VIC-bank/runtime regressions
  - serial shift behavior from CRA bit 6, TOD behavior from CRA/CRB bit 7, FLAG/PC
    handshakes, and cycle-race/pin-level output timing remain deferred to later phases
- CIA Phase D interrupt behavior:
  - ICR flags and masks are separate; writes to `$0D` update masks only and do not clear
    pending source flags
  - ICR write bit 7 selects set-mask vs clear-mask, and bits 0-4 select the affected
    Timer A, Timer B, TOD alarm, serial complete, and FLAG source masks
  - CPU-visible ICR reads return source flags plus bit 7 only when at least one enabled
    source is pending, then clear the reported source flags and update the output line
  - debugger-safe ICR peeks preserve flags and output state
  - Timer A and Timer B underflows set their ICR source flags through a generic
    `cia_set_interrupt_source()` helper; reserved TOD/serial/FLAG source masks are
    handled even though their event generation is deferred
  - CIA #1 enabled-pending sources drive the CPU IRQ aggregate path alongside VIC-II IRQ
  - CIA #2 enabled-pending sources drive CPU NMI through an edge latch, preserving the
    existing RESTORE NMI path and avoiding repeated NMI entries while the CIA2 line stays
    asserted
  - regression coverage includes masked vs enabled timer flags, ICR set/clear mask
    writes, normal read-clear/deassert behavior, reserved source mask semantics, CIA #1
    timer IRQ vector entry, CIA #2 timer NMI vector entry, CIA2 NMI edge behavior, and
    RESTORE NMI preservation
  - TOD alarm, serial complete, FLAG line event generation, and pin-level interrupt race
    behavior remain deferred to later CIA phases
- CIA Phase E CIA #1 port behavior:
  - generic CIA port reads now support external active-low line pulls while preserving
    output latches and DDR-selected input/output behavior
  - CIA #1 wiring lives at the machine layer through a port-input callback rather than
    in frontend or runtime code
  - keyboard matrix scans work in both directions: Port A row output selection affects
    Port B column reads, and Port B column output selection affects Port A row reads
  - multiple simultaneous keys combine as active-low electrical pulls on shared lines
  - joystick port 1 pulls CIA #1 Port B bits 0-4 low for up/down/left/right/fire
  - joystick port 2 pulls CIA #1 Port A bits 0-4 low for up/down/left/right/fire
  - keyboard and joystick inputs combine on the same CIA #1 lines, so either source can
    pull a bit low
  - RESTORE remains outside the ordinary keyboard matrix and continues through the CPU
    NMI request path
  - regression coverage includes bidirectional keyboard scanning, multi-key scans,
    joystick port 1 and port 2 bits, keyboard-plus-joystick shared-line behavior,
    debugger-safe port peeks, and RESTORE isolation from matrix reads
  - light-pen/fire-button side effects, host joystick/frontend mapping, and cycle-perfect
    keyboard/joystick sampling remain deferred
- CIA Phase F CIA #2 port behavior:
  - VIC-II bank selection derives from effective CIA #2 Port A pin levels for PA0/PA1
    using the C64 inverted bank mapping, so DDRA input bits read released-high instead
    of using raw latch writes
  - CIA #2 wiring lives at the machine layer through the generic CIA port-input callback
  - IEC ATN, CLK, and DATA are represented as active-low/open-collector lines on CIA #2
    Port A: PA3 drives/senses ATN, PA4 drives CLK with PA6 as CLK sense, and PA5 drives
    DATA with PA7 as DATA sense
  - released IEC lines read high; CIA output pulls and an external pull mask can hold
    lines low until all sources release them
  - CIA #2 timer interrupt routing to CPU NMI remains covered by the Phase D regression
    after the CIA #2 port wiring changes
  - regression coverage includes DDRA-sensitive VIC bank selection, IEC released-high
    reads, CIA-driven ATN/CLK/DATA low reads, external DATA pull/release behavior,
    existing VIC bank rendering tests, and CIA #2 NMI entry
  - disk-drive emulation, full IEC protocol timing/state machines, and CIA serial data
    register shifting remain deferred
- CIA Phase G TOD behavior:
  - TOD state is explicit CIA state for tenths, seconds, minutes, hours, alarm, and
    read latch rather than raw `$08-$0B` passthrough
  - CRA bit 7 selects the TOD source policy: clear uses a 60 Hz source and set uses a
    50 Hz source; machine setup configures the 50 Hz tenth cadence as 5 PAL frames
    (`63*312*5` cycles) and the 60 Hz tenth cadence as 6 NTSC frames (`65*263*6`)
  - TOD values are stored/read as BCD; invalid BCD writes are normalized to a
    deterministic zero value, while invalid hours normalize to 12
  - TOD hours use bit 7 as PM and 12-hour rollover: 11:59:59.9 AM advances to
    12:00:00.0 PM, and 12:59:59.9 PM advances to 1:00:00.0 PM
  - CPU-visible TOD hour reads latch a coherent snapshot, subsequent TOD reads use that
    snapshot, and reading TOD tenths releases the latch
  - debugger-safe TOD peeks expose the current latched/live TOD value without creating or
    releasing the CPU-visible latch
  - CRB bit 7 selects alarm writes; alarm matches set ICR bit 2 and route through the
    Phase D IRQ/NMI mask and output behavior
  - regression coverage includes selected 50/60 Hz cadence, machine cadence constants,
    BCD second/minute/hour rollover, AM/PM behavior, coherent read latching,
    debugger-safe peeks, alarm writes, TOD ICR flagging, and CIA #2 TOD alarm NMI entry
  - pin-perfect TOD input, hardware variant differences, TOD write stop/resume edge
    timing, and latch/read races remain deferred to later accuracy work
- ROM boot progression:
  - machine/runtime boot checkpoint counters
  - IRQ vector entry validation through the machine bus
  - IRQ stack push validation
  - ROM-driven screen RAM writes reflected in frames
  - ROM-driven color RAM writes reflected in frames
  - VIC-II `$D018` screen/character pointer support
  - real 64C ROM smoke checkpoint reaches VIC/CIA/screen activity
- Keyboard Pass 1 plumbing:
  - machine-owned C64 keyboard matrix
  - key press/release state
  - CIA #1 keyboard scan reads through `$DC00/$DC01`
  - runtime copied key down/up commands
  - SDL key mapping for letters, digits, space, return, delete, shift, and common BASIC punctuation keys
  - semantic host cursor arrows:
    - right/down map to C64 cursor keys
    - left/up synthesize Shift + C64 cursor keys
  - ESC maps to C64 RUN/STOP
  - Backspace maps to C64 DEL; Shift+Backspace maps to C64 INST (insert)
  - host Delete maps to RESTORE
- Keyboard Pass 2 / Phase 11 BASIC typing polish:
  - SDL-to-C64 key translation moved out of `main.c` into frontend-owned input mapping
  - runtime still receives copied project-level keyboard/RESTORE commands, not SDL events
  - default mapping is semantic host typing rather than physical C64 key layout
  - focused frontend mapper regression tests cover shifted punctuation, remembered synthetic releases, cursor keys, CONTROL, Commodore, and RESTORE
  - C64 CONTROL is mapped from host Control
  - C64 Commodore is mapped from host Tab
  - emulator controls use Option+R run, Option+S step, and Option+P pause
  - F10/F11/F12 remain available for run/step/pause
  - host quote/double-quote, colon, plus, parentheses, asterisk, @, cursor arrows, HOME/CLR HOME, RUN/STOP, RESTORE, left-arrow, and up-arrow have semantic mappings
  - Shift+letter preserves the C64 left graphics character set
  - Tab+letter provides the C64 Commodore graphics character set
  - manual BASIC validation transcript added for:
    - `10 PRINT "HELLO"`
    - `20 GOTO 10`
    - `RUN`
- Paste from host OS clipboard:
  - Option+Insert triggers a clipboard paste into the running emulator
  - each character is translated from ASCII to a C64 key + shift combination and
    injected via the keyboard matrix state machine in the runtime thread
  - timing is cycle-accurate: each key is held for ~40ms equivalent in emulated
    PAL cycles with a ~10ms inter-key gap, so paste rate scales correctly with
    turbo multiplier
  - mapping covers all ASCII printable characters the C64 keyboard can produce:
    letters (A–Z), digits (0–9), unshifted symbols (`@ * + - = : ; , . /`),
    shifted digit symbols (`! " # $ % & ' ( )`), shifted punctuation
    (`< > ? [ ]`), and `^` via the up-arrow key; unmappable characters are silently
    skipped
- IRQ/CIA boot compatibility:
  - CIA #1 ICR read/write diagnostics
  - CIA interrupt assertion diagnostics
  - CPU IRQ entry diagnostics
- CPU NMI entry path for RESTORE
  - CIA zero-latch timer reload behavior
  - CIA one-shot timer stop behavior
  - normal runtime RUN pacing at roughly 60 Hz frame cadence
- App startup:
  - reset screen starts clear
  - frontend queues Run automatically after initialization
- SDL display of machine-generated frames.
- Phase 12 debugger UI foundation is complete:
  - CPU/register, disassembly, memory, misc/debugger, breakpoint list, debugger input routing, and runtime snapshot/command plumbing are implemented
- Phase 12 debugger UI foundation, View 1:
  - slim CPU/register view renders from copied runtime CPU snapshots
  - PC, SP, A, X, Y, and `N V - B D I Z C` flags display in fixed-width uppercase/readable form
  - paused register/status edits emit frontend debugger intents
  - `main.c` translates debugger intents into runtime_client commands
  - runtime owns and applies CPU register mutations only while paused
  - running register mutations are ignored by runtime
  - regression coverage validates paused CPU register setters and running-state rejection
- Phase 12 debugger UI foundation, View 3:
  - memory view renders copied runtime memory snapshots in compact uppercase 16-byte hex rows plus ASCII
  - CPU-map and raw RAM memory modes are supported
  - CPU-map snapshots use debugger-safe peeks that do not perform side-effecting CIA reads
  - cursor movement, PageUp/PageDown, Home/End, and hex/ASCII/address modes are wired
  - custom visible cursor is drawn for address, hex-nibble, and ASCII edit positions while paused
  - custom scrollbar thumb can be dragged across the 64K address space
  - paused hex and ASCII byte edits emit frontend debugger intents and accept key repeat
  - runtime owns and applies memory writes only while paused
  - running memory writes are ignored by runtime
  - regression coverage validates memory snapshots, paused writes, and running-state rejection
- Phase 12 debugger UI foundation, View 2:
  - disassembly view renders compact decoded 6502 lines from copied runtime memory snapshots
  - decoder lives in `src/tools/disasm_6502` and does not own or read machine state
  - CPU-map and raw RAM disassembly modes are supported
  - current PC rows are highlighted and drive the view while running or stepping
  - transient user cursor is created only by paused navigation, is cleared by run, and preserves its address while off-screen
  - PageUp/PageDown, Home/End, Up/Down, follow-PC, scrollbar, and basic address-entry navigation are wired
  - running debugger refreshes CPU/machine snapshots at frame cadence
  - symbol resolver hooks exist and currently default to not found
  - breakpoint rendering/toggling uses runtime-owned execute breakpoint snapshots
  - regression coverage validates core decoder formatting and symbol lookup behavior
- Tools-level debug symbol table:
  - `src/tools/symbols` implements a frontend/debug-session-owned symbol table,
    separate from emulator machine state and separate from the assembler's
    internal symbol machinery
  - symbols store an owned name, 16-bit address, source kind, and owned exact
    source name
  - source kinds cover file, assembler, user, and built-in symbols
  - exact address lookup uses a 65536-entry primary index storing entry indexes
  - name lookup uses `stb_ds` and keeps one current binding per name
  - `symbol_table_add()` supports deterministic conflict handling with explicit
    overwrite behavior; v1 stores only active/current symbols, not same-address
    aliases or history
  - `symbol_table_remove_source()` and `symbol_table_clear()` support reassembly,
    reload, and session reset workflows
  - nearest-symbol lookup is available for future debugger/UI use, while current
    disassembler formatting remains exact-only
  - `symbol_table_make_resolver()` backs the existing disassembler
    `symbol_resolver` interface
  - `stb_ds` implementation is centralized in `external/stb/stb_ds_impl.c`
    instead of being embedded in individual users
  - regression coverage validates add/find, conflict and overwrite behavior,
    source removal, nearest lookup, resolver enumeration, and disassembler
    integration
- Phase 12 debugger UI foundation, View 4 breakpoint pass:
  - misc/debugger view is now organized as scrollable tabs: Programs, Debugger, Breakpoints, and Hardware
  - Programs tab can select a `.prg` file and send it to runtime for direct RAM loading at the PRG load address
  - D64 disk mounting and CRT cartridge loading remain deferred
  - runtime owns execute breakpoints with stable IDs, enabled/disabled state, and hit counters
  - runtime owns and publishes a copied stop reason in machine snapshots
  - runtime_client supports set, clear, clear-all, enable/disable, and snapshot request commands
  - runtime checks enabled execute breakpoints while running, stepping, and bounded-running, then pauses and publishes copied state
  - frontend renders breakpoint snapshots only, with disabled breakpoints kept visible as bookmarks
  - disassembly View 2 shows copied breakpoint snapshots in the gutter and Option+B toggles an execute breakpoint at the cursor while paused
  - misc/debugger view shows debug status, stop reason, cycle/frame counters, breakpoint rows, View PC, Enable/Disable, Clear, and conditional Clear All
  - Phase 12 execute-only breakpoint behavior is retained as the quick-toggle path
- Phase 13 breakpoint/watchpoint system:
  - runtime breakpoint model supports stable runtime IDs, duplicate addresses/ranges, enabled state, start/end address ranges, access masks, mapping filters, action masks, hit counts, and counters
  - runtime_client supports create, update, duplicate, clear, clear-all, enable/disable, and copied breakpoint snapshot commands
  - runtime evaluates execute/read/write breakpoints and watchpoints, including inclusive ranges and Map/ROM/RAM filters
  - machine reports generic CPU memory access events to runtime; machine does not know debugger UI concepts
  - runtime uses machine-side visibility decoding for ROM/RAM filters, with IO matching Map only
  - counters are runtime-owned; count zero triggers immediately, reset zero triggers every later match, and disabled breakpoints do not decrement counters
  - runtime action framework supports Break, Fast, Slow, Tron, Troff, Type, and Swap action masks
  - Break pauses before later state-changing actions; non-Break actions do not pause
  - Fast switches runtime pacing to maximum turbo mode; Slow restores normal paced mode
  - Tron/Troff update runtime trace state; Type and Swap are Phase 13 no-ops pending later implementation
  - `[DEBUG]` INI persistence loads and saves `break.<address>` entries, supports duplicate suffixes such as `.1` and `.2`, and skips invalid entries while loading remaining valid breakpoints
  - breakpoint editor modal supports create, edit, duplicate, access checkboxes, start/end range, mapping selection, counters, action checkboxes, validation, cancel, and apply
  - frontend renders copied breakpoint snapshots only and sends edits through runtime_client
- Debugger input routing:
  - C64 display input is the initial/default focus, including the first time the debugger UI is opened
  - clicking the C64 display while the debugger UI is visible returns ordinary key input to the emulated C64
  - clicking debugger views returns ordinary key input to the focused debugger view, and that choice survives UI hide/show toggles
- Phase 14 INI configuration system and configure dialog:
  - Misc view Programs tab has been renamed to Machine and now opens a Configure dialog
  - Configure dialog owns original and edited configuration copies; edits are temporary until OK and Cancel discards them
  - Machine tab exposes video standard, display width/height, integer scaling, aspect correction, and filter settings
  - Emulator tab exposes scroll wheel speed, turbo speeds, disk LED visibility, symbol file paths, and persistent auto-save
  - global INI file path editing and native picker flow are wired, including existing-file Yes/No/Cancel parse behavior
  - changing the INI filename automatically enables the one-shot Save INI on Quit flag when saving is allowed
  - `--nosaveini` disables save controls; `--noini` skips startup INI loading without disabling later saves
  - OK applies immediate frontend/app settings, sends copied machine config through runtime_client, and reboots on PAL/NTSC changes
  - VIC-II timing now supports NTSC and PAL line/frame timing selected from machine configuration
  - `[config]` INI keys now persist scroll wheel speed, turbo speeds, symbol files, and `Save=yes`
  - turbo speed CSV is parsed into runtime-owned available multipliers; the first entry becomes the active paced multiplier
  - Option-T cycles the active paced turbo multiplier through the configured turbo speed list
  - symbol file changes currently trigger view refresh plumbing only; real symbol
    file parsing and UI-driven load/unload into `src/tools/symbols` remain future work
- Phase 16 timed bus event and live VIC-II raster foundation:
  - machine owns a monotonic master cycle and advances VIC-II/CIA/SID hooks to
    timestamped CPU bus event cycles before applying CPU-visible side effects
  - CPU instruction stepping remains the external runtime/debugger API, while
    bus-visible reads/writes are classified and timestamped within each opcode
  - CPU writes are not applied twice: deferred/timed paths record writes first and
    apply RAM/I/O/device side effects only when machine time reaches the event
  - VIC-visible writes to registers such as `$D020` take effect at their event cycle
    rather than only after opcode completion or at whole-frame snapshot time
  - runtime running-frame publication now copies completed live VIC-II frame buffers;
    the old whole-frame snapshot renderer remains only as a compatibility/debug path
    before a live frame has completed
  - live raster rendering emits border/background and current Phase C display pixels
    as VIC time advances, including standard text, multicolor text, standard bitmap,
    multicolor bitmap, ECM text, and invalid modes 5/6/7
  - mid-frame border color changes are visible in completed/published frame output;
    regression coverage proves a timed `$D020` write changes only later border pixels
  - current Bad Line BA handling now routes through CPU event read/write
    classification: read cycles stall while BA is low, write-only cycles continue,
    and unknown/internal cycles remain conservatively stalled
- VIC-II Phase D — Sprites Display:
  - all 8 hardware sprites can be independently positioned and displayed
  - 9-bit X coordinate (`$D000`/`$D002`…`$D00E` + MSB register `$D010`) and 8-bit Y
    coordinate (`$D001`/`$D003`…`$D00F`) with per-sprite enable via `$D015`
  - hires mode (1 bit/pixel, 24×21): sprite color from `$D027`–`$D02E`
  - X-expand (`$D01D`): doubles sprite width to 48 pixels
  - Y-expand (`$D017`): doubles sprite height to 42 raster lines via per-sprite
    flip-flop that gates `mc` advance on alternate lines
  - multicolor mode (`$D01C`): 2 bits per logical pixel pair — transparent / MM0
    (`$D025`) / sprite color / MM1 (`$D026`); combined X+Y expand works correctly
  - sprite pointer (p-access) fetched from `vic_bank + screen_base + $03F8 + n`;
    sprite data (s-access) fetched as 3 bytes from `vic_bank + pointer × 64 + mc`
  - VIC bank base derived from CIA 2 port A bits 1–0 (inverted) via
    `c64_bus_vic_bank_base()`; default bank is `$0000`
  - `vicii_fetch_sprites()` called at cycle 0 of each raster line; sets
    `sprite_visible[n]` and fills `sprite_data[n][3]` for the live renderer
  - sprite pixel candidates are decoded in `vicii_live_pixel()` after background
    pixel computation; Phase E owns final priority composition
  - `vicii_snapshot_sprite_line()` computes sprite row data statically per raster
    line for the snapshot renderer; overlay loop mirrors live-pixel compositing
  - horizontal wraparound (modulo 512) handled by `vicii_sprite_dx_wrapped()`
  - regression test `test_sprite_hires_appears_at_position` confirms yellow pixels
    appear at the correct frame coordinates for a fully-opaque hires sprite
- VIC-II Phase E — Sprite Priority and Collision Detection:
  - `$D01B` sprite-background priority is implemented; sprites with priority bits
    clear appear in front of foreground graphics, and sprites with bits set appear
    behind foreground graphics but remain visible over background pixels
  - final pixel composition order is border, front-priority sprites, foreground
    graphics, behind-priority sprites, then background graphics
  - standard text, multicolor text, standard bitmap, multicolor bitmap, ECM text,
    and invalid modes now expose foreground/non-foreground classification to the
    sprite compositor and collision logic
  - `$D01E` sprite-sprite collision latch sets all participating sprite bits when
    two or more non-transparent sprite pixels overlap; reads return and clear the
    latch, and writes are ignored
  - `$D01F` sprite-background collision latch sets sprite bits when non-transparent
    sprite pixels overlap foreground graphics pixels; reads return and clear the
    latch, and writes are ignored
  - IMMC (`$D019/$D01A` bit 2) and IMBC (`$D019/$D01A` bit 1) are wired through the
    existing VIC IRQ status/enable path, including retriggering after `$D019` is
    cleared even if the collision latch still contains prior bits
  - collision detection uses decoded, expanded sprite pixels and runs before visual
    priority selection; `$D01B` does not suppress collision detection
  - sprite-sprite collision detection still runs under the border while border
    pixels remain visually in front of sprites
  - regression coverage validates `$D01B` readback, `$D01E/$D01F` read-clear and
    write-ignore behavior, sprite-sprite collision IRQs, sprite-background priority
    and collision behavior, and border-over-sprite collision latching
- VIC-II Phase J — DEN-Off Visual Blanking:
  - clearing `$D011` bit 4 (`DEN=0`) visually blanks display and border-visible pixels
    to background color 0 (`$D021`) instead of drawing character/bitmap foreground
    pixels or the normal `$D020` border color
  - sprite visibility in the display area remains governed by the existing sprite
    priority path
  - underlying background foreground classification is preserved while the visible
    color is blanked, so sprite-background collision via `$D01F` still latches when
    an opaque sprite overlaps an underlying foreground graphics pixel
  - sprite-sprite collision via `$D01E` continues to latch with `DEN=0`
  - regression coverage validates DEN-on text foreground rendering, DEN-off visual
    blanking, DEN-off border blanking, DEN-off sprite visibility, and DEN-off
    sprite/background and sprite/sprite collisions

- VIC-II Phase F (light pen): skipped — not implemented; $D013/$D014 remain stubbed.
- VIC-II Phase G — Open Bus / Unused Register Reads:
  - `$D016` bits 7:5 are unused and read as 1; bits 4:0 (MCM/CSEL/XSCROLL) continue
    to reflect written state
  - `$D020`–`$D02E` (border, background 0–3, sprite multicolor 0–1, sprite colors 0–7)
    bits 7:4 are unused and read as 1; bits 3:0 continue to reflect the written color index
  - `$D02F`–`$D03F` (unused register block) all reads return `$FF`; existing register
    mirroring ($D000–$D3FF via `addr & 0x3F`) means mirrored addresses ($D12F, $D22F,
    $D32F, etc.) also return `$FF` without any additional code
  - internal register storage is unchanged — writes still store full bytes; only the
    read path applies the above masks
  - `$D018` has no Phase G masking; read behavior is unchanged from pre-Phase-G
  - `$D019` bits 6:4 read as 1 and bit 7 reflects aggregate enabled-pending IRQ
    summary: re-verified by existing `test_irq_status_high_bit_reports_enabled_pending_irq`;
    not reimplemented
  - `$D01A` bits 7:4 read as 1: re-verified by the same existing test; not reimplemented
  - `$D01E`/`$D01F` read-clear and write-ignore behavior: re-verified by existing
    `test_sprite_collision_registers_read_clear`; not reimplemented
  - last-byte-on-bus stretch goal: not attempted (not authorized by human maintainer)
  - regression tests added: `test_d016_unused_high_bits_read_as_1`,
    `test_color_register_high_nibble_reads_as_1`, `test_unused_register_block_reads_ff`,
    `test_unused_register_block_mirrored_reads_ff`, `test_d018_no_phase_g_masking`

- VIC-II Phase H — Sprite-Fetch BA Cycle Stealing:
  - sprite p-access/s-access windows now contribute BA-low periods that stall the CPU,
    using the same read/write classification the Bad Line BA path already uses
  - BA is represented as absolute-cycle expiry fields (`ba_low_until_abs`,
    `sprite_ba_low_until_abs` in `vicii_timing`) so that cross-line sprite windows
    (sprites 3 and 4, whose BA starts at cycles 60 and 62 of the previous raster line)
    are handled correctly without line-relative wrap arithmetic
  - `vicii_step_cycle()` and `vicii_ba_active()` now receive the absolute machine cycle
    as a parameter; all callers in `c64.c` pass `machine->clock.cycle`
  - PAL 6569 sprite BA schedule encoded in `vicii_pal_sprite_ba_assert[8]`: sprites 0–2
    assert at current-line cycles 54, 56, 58; sprites 3–4 assert at previous-line cycles
    60, 62 (cross-line via `vicii_sprite_dma_next_line()`); sprites 5–7 at cycles 1, 3, 5
  - BA window width is always 5 cycles (`ba_start = p_cycle − 3`, `ba_end = p_cycle + 2`)
  - `sprite_ba_low_until_abs` is a rolling high-water mark; each new assertion takes
    `max(current, abs_cycle + 5)` so overlapping windows within the early (sprites 3–7)
    or late (sprites 0–2) group merge without over-extending into the gap between groups
  - AEC is not modeled as emulator state by design; `vicii_ba_active()` is the sole
    stall predicate consumed by the CPU stall path — no AEC-named field, function,
    or snapshot surface was added
  - NTSC sprite timing is deferred: PAL table only; NTSC requires a separate
    `sprite_ba_assert` table entry and is left as an explicit TODO in `vicii.c`
  - no debugger visibility was added for BA/sprite-fetch state; the existing stall
    mechanism is sufficient for correct emulation without additional snapshot fields
  - regression tests added: `test_sprite5_ba_window_within_line`,
    `test_sprites567_adjacent_ba_union`, `test_6sprite_ba_early_and_late_windows`,
    `test_inactive_sprites_no_ba`, `test_sprite3_cross_line_ba`,
    `test_sprite4_cross_line_ba`, `test_aec_absent_ba_is_sole_stall_predicate`

- Machine reset and PRG loader polish:
  - RESET button added to the Misc > Machine tab; wired through a new `FRONTEND_INTENT_RESET`
    intent dispatched to `runtime_client_reset()` in `main.c`
  - `RUNTIME_COMMAND_RESET` remembers whether the runtime was running before the reset and
    resumes automatically if it was, so RESET behaves like a real C64 reset rather than
    halting the emulator
  - PRG loader now follows the same run-state contract: remember running state, reset,
    load, resume
  - PRG load event order is deterministic: reset completion is published first, the
    PRG memory snapshot is published after deferred injection, then the final paused or
    running state event is published according to the pre-load runtime state
  - Collection PRGs (PRGs that pre-fill the C64 keyboard buffer at `$C6`/`$0277–$0280` to
    auto-start games) work correctly with the reset-before-load path via deferred injection:
    - after reset, the machine is set running so Kernal RAMTAS and BASIC cold start execute
      fully (RAMTAS clears zero page including `$C6`, so injecting before this point is
      ineffective)
    - the PRG path is held in `runtime.pending_prg_path`; the inner run loop watches for
      `cpu.pc == $E38B` (Kernal warm-start / BASIC READY entry point)
    - at that PC the PRG bytes are injected into RAM — keyboard buffer pre-fills survive
      because all initialization is complete and BASIC has not yet read the buffer — then
      execution continues normally, BASIC prints "READY." and processes the injected keys
    - a manual RESET cancels any pending PRG injection by freeing `pending_prg_path`

- VIC-II bank-aware character and screen rendering:
  - all VIC memory reads (screen RAM, character glyphs, bitmap data) now use the
    full absolute VIC address: `vic_bank + within-bank offset`
  - `vic_bank` is derived from CIA 2 port A pins bits 1–0 (inverted) via
    `c64_bus_vic_bank_base()` and applied in `vicii_live_pixel()`,
    `vicii_make_frame_snapshot()`, and the Bad Line c-access fetch in
    `vicii_step_cycle()`
  - character glyph fetch routes to char ROM when the full address falls in
    `$1000–$1FFF` (VIC bank 0) or `$9000–$9FFF` (VIC bank 2); all other
    addresses read from RAM, enabling user-redefined character sets
  - `c64_reset()` initialises `$D018 = $15` (screen at `$0400`, charset at
    `$1000`) matching the KERNAL default, so the ROM character set is visible
    before any game or KERNAL init code writes `$D018`
- Assembler UI integration (ASMDESIGN.md Phase 16):
  - Assembler tab added to the Misc panel as the fifth tab (Machine / Debugger / Breakpoints / Hardware / Assembler)
  - File Name field with OS file-picker Browse button, Address and Run Address hex fields, Auto Run checkbox, and Assemble button
  - Run Address field tracks the Address field until the user manually edits it (`run_address_user_edited` flag)
  - Assembly uses the reset-and-run-to-BASIC flow: machine resets, runs to $E38B, then assembles at the given address — mirrors the PRG loader pending-path pattern
  - Auto Run sets `cpu.pc = run_address` and `cpu.sp = 0x01FF` then resumes the emulator
  - Emulator always resumes after assembly regardless of success or failure
  - New `RUNTIME_EVENT_ASSEMBLE_ERROR` event (distinct from `RUNTIME_EVENT_ERROR`) carries assembler diagnostics; frontend shows a scrollable per-line error dialog with OK button
  - Runtime thread owns a `symbol_table *symbols`; after successful assembly, symbols are serialized into a mutex-protected `runtime_symbol_slot` (mirrors `runtime_frame_slot`); frontend polls via `runtime_client_poll_symbols()` and rebuilds its own `symbol_table *` then calls `symbol_table_make_resolver()` so the disassembler view shows assembler labels immediately
  - `RUNTIME_SYMBOL_SNAPSHOT_MAX = 256` symbols, `RUNTIME_SYMBOL_NAME_MAX = 64` chars; symbols beyond 256 are silently capped with total still reported

## Not Implemented

- VIC-II remaining accuracy/features:
  - light pen is not implemented (Phase F skipped)
  - open-bus / last-byte-on-bus (last fetched byte returned for open addresses) is not
    implemented; unused register addresses return fixed $FF per Phase G
  - BA now covers both Bad Line c-access windows and sprite p-access/s-access
    windows (Phase H); AEC is not modeled and is intentionally absent by design —
    it is a documentation/hardware concept only, not an emulator predicate
  - exact RDY/AEC sub-cycle timing (CPU pin-level accuracy) is not modeled
  - idle-state g-access fetch behavior from `$3FFF` / `$39FF` is not modeled in the
    renderer
- Phase 13 deferred breakpoint action details:
  - Type text injection is not implemented yet
  - Swap disk behavior is not implemented yet
  - Trace output/details are not implemented yet
- Full CIA accuracy.
- SID.
- Cycle-perfect video/audio timing.

## Current Runtime Notes

Real 64C ROM execution reaches the BASIC READY prompt with a visible cursor and keyboard input.

After a 1,000,000-cycle smoke trace:

```text
PC=$FD7E
CIA #1 IRQ pending = true
CPU IRQ entries = 0
CIA #1 ICR reads = 0
```

The pending CIA #1 IRQ is not currently observed as a CPU IRQ entry because the CPU interrupt-disable flag remains set during the trace.

## Phase 12 UI Notes

- Compact debugger text views should use the small Nuklear default font currently installed in `frontend_create` and row heights based on `ctx->style.font->height`, not oversized fixed row heights.
- For dense text views, temporarily zero Nuklear window padding, spacing, and group padding inside the view body, then restore the saved style before leaving the window.
- Custom scrollbars should live in their own layout column/group, not as an overlay drawn on top of the text area or near splitter hit zones.
- Scrollbar drag state must be exclusive with layout splitter drag state. `frontend_render` suppresses layout dragging while the memory scrollbar owns the mouse.
- Scrollbar thumb dragging should start only from the thumb, track grab offset, and map thumb movement back to the debugger view address. Track clicks may page-jump separately.
- Avoid `nk_input_has_mouse_click_in_rect` for row selection in custom text views; it can keep matching the original click position while mouse state is tracked. Use a press-edge plus current hover test instead.
- Artificial cursors for debugger text views should be drawn on the window canvas at computed font-cell coordinates. Do not rely on Nuklear edit widgets for hex dump/disassembly cursors.
- Editable debugger cursors should be hidden while the runtime is running if edits are gated to paused state.
- Repeated keydown events are allowed for focused debugger text editing, while global emulator controls and Option/F-key commands remain owned by the master input layer.
