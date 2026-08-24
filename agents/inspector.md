# Inspector: time-travel debugger (roadmap)

**Status:** I0–I1 landed. I2–I4 not started.  
**Product name:** Inspector — runtime-owned time travel (checkpoints + land + sealed re-execute).  
**Not:** the CPU flight recorder (HST1). That is a separate forensic product.  
**Depends on:** sessions S0–S4 (C64M/7, closed); machine snapshots (`c64_snapshot_save` / `load`); frame ring (film preview); guarded breakpoints (the one BP list).  
**Unblocks:** one-skin live + Inspect debugger.  
**Out of campaign:** Promote / Branch. Do not open an I5 for it.

Related: [`README.md`](README.md) · [`sessions.md`](sessions.md) ·
[`cpu-flight-recorder.md`](cpu-flight-recorder.md) · [`frame-ring-plan.md`](frame-ring-plan.md) ·
[`machine.md`](machine.md) · [`control-port.md`](control-port.md) · [`frontend-debugger.md`](frontend-debugger.md).

Sibling product in `../a2m` shipped under the name TimeMachine. c64m does **not** use that name, those filenames, or that wire vocabulary. Consult a2m **code** for behaviour; implement from this brief.

Source is authoritative once phases land. If this brief and code disagree, fix the brief in the same change.

---

## Two products (do not conflate)

| Product | What it is | Product use |
|---------|------------|-------------|
| **Flight recorder (HST1)** | Instruction log | **Forensic.** FIND: "who wrote `$22` to `$D020`". Already shipped (`cpu-flight-recorder.md`). |
| **Inspector** | Checkpoint snapshots + input log + land + sealed re-execute | **Time travel.** Misc Inspector tab, film / land / `±` / F10-family. |

Inspector is not forensic. HST1 is not the Inspector slider and is not F12.

HST1 already records by default. Inspector recording is **opt-in** and **does not** arm HST1.

---

## Why

Today the live debugger talks to the live `c64_t`. The frame ring keeps stills. HST1 keeps instruction rows. None of those restore the machine to a past cycle.

Target:

```text
Live:      UI  <->  Machine           (execute opcodes)
Inspect:   UI  <->  Inspector         (land a checkpoint; re-execute toward live)
```

Inspector answers **queries** (land-at-cycle, frame-step, re-execute to breakpoint or live). The one true `c64_t` **is** the past while Inspecting, so existing views just work.

---

## Vocabulary

| Term | Meaning |
|------|---------|
| **Inspector** | This product: recording + mode + Misc tab. |
| **Record** | Checkpoint ring + input log (+ film arm) is rolling. CLI `--inspector`. |
| **Inspect** | Mode: the machine is a reconstructed point on the Inspector timeline. |
| **Live** | Right end of that timeline: the paused NOW taken when Inspect was entered. Not "keep executing the live line." |
| **Film** | Frame-ring **indexed8** stills (what the CRT showed while recording). Preview only. |
| **Film cursor** | Slider position while the thumb is down. Does not move the C64. |
| **Land** | Release the thumb: load nearest checkpoint <= that time (or restore **live** at the right end). One true state becomes that snapshot. Paint from it. |
| **Pink** | Host RGB fill `(255,0,255)` of the CRT **client area** when there is no film at the film cursor. Not an indexed8 colour. Loud on purpose. |
| **Frame-step** | `[-]` / `[+]` move one guest VIC-II frame, then paint. Clamped to the timeline. |
| **Sealed replay** | Re-execution with observers / audio / host media side effects muted. |
| **NOW blob** | Full `c64_snapshot_save` taken at Inspect enter. Leave restores it. Infallible. |

Do not say TimeMachine. Do not say `forensic` for this product. Do not say the slider "seeks the tape."

---

## Key decisions (pinned — do not re-litigate in phase scopes)

| # | Topic | Decision |
|---|-------|----------|
| D1 | Product | **One debugger skin.** Inspect = mode of F9; leave = live. Same keys; mode-aware verbs. |
| D2 | Truth | **One true machine state.** In Inspect, Inspector **replaces** live `c64_t` with reconstructed past (read-only pokes). Views keep talking to RAM/CPU/VIC/CIA/SID/1541. |
| D3 | Cursor | Inspect active -> THEN. Leave Inspect -> live NOW. No simultaneous NOW+THEN panels in V1. |
| D4 | Mutation | Pokes **reject**. Execute-forward is how the cursor moves (F10-family, `[+]`, F12). Nothing executes **past live**. |
| D5 | Storage | **Checkpoint ring + input log + sealed re-execution.** A checkpoint is `c64_snapshot_save` into a ring slot (CPU/RAM/VIC/CIA/SID/cart/1541 as the serializer already covers). **No write-delta stream.** Do not reconstruct banking or VIC from HST1. |
| D5a | Why not deltas | A checkpoint is the existing snapshot; a byte-budget of slots bounds replay to about one cadence of cycles. Re-execution reproduces VIC paint, 1541 mechanics, CIA/SID microstate for free. A delta stream reproduces none of those. Do not re-open without new measurements. |
| D6 | Recording | **Opt-in.** INI/CLI + Inspector tab Record. Off = no Inspector burden. On = checkpoints, input log, and film as configured. HST1 stays its own switch. |
| D7 | Threads | Inspector runs on the **runtime worker**. No second thread in V1. |
| D8 | Ownership | Runtime-owned (`src/runtime/runtime_inspector*`). Machine-like API. UI only via `runtime_client`. Identifiers are `runtime_inspector_*`, never `tm_*` / `timemachine`. |
| D9 | Sessions / control | **Cooperative one state.** `get-memory` etc. see the one true state (past while Inspecting). Land is a shared mutation; `state-changed` informs peers. No exclusive lock / dual-world. |
| D10 | Media | **A guest media write that succeeds cuts the window.** A refused write (write-protect) changes nothing and must not cut. Records/checkpoints/frames older than the write are dropped; recording continues forward. Media bytes live outside the checkpoint, so a replayed read would return present-day bytes. 1541 mechanical state rides in the checkpoint as normal. Key on **guest** writes, not housekeeping (eject flush, save-state, `sync_dirty` export). |
| D11 | Audio | SID chip state rides in the checkpoint and re-execution reproduces it. Host **audio output** is suppressed during replay. No attempt at continuous Inspect audio. |
| D12 | Nav direction | Forward = keep executing under the seal / walk toward live. Backward = **rebuild** from an earlier checkpoint + re-execute to target — not reverse-CPU. |
| D13 | Promote / Branch | **Out of campaign.** Do not implement. |
| D14 | Second shell | **Do not build one.** F9 is the debugger. Host F7 stays unbound for Inspector (C64 F7 is a guest key). |
| D15 | Entry UX | Misc -> **Inspector** tab. Record / Inspect / Leave. Net-new tab. |
| D16 | Sealed replay | Re-execution must run **sealed**: CPU observer off, memory-access callback off, no frame-ring push / live publish until stop, no host audio, no host media write-through. A leaky seal corrupts the tape being stood on. One ctest per gate. **During** execute only: when F10-family / F12 / Pause **stops**, present the CRT (A16). |
| D17 | Timeline | Inspector timeline = **oldest retained checkpoint -> live** (the NOW blob). Single interval, never islands. A media cut (D10) or max/warp wipe (I4) moves `oldest` rather than leaving a gap. HST1 and the VIC ring are **not** in this intersection. Film is optional (pink if missing). Materialize / land outside the timeline is an honest error, never a partial apply. |
| D18 | Control honesty | Inspect is a **global** read-only (pokes) state, so socket peers must see it and leave it. Status reports `mode` + focus cycle; a leave verb exists; `state-changed` gains Inspector reasons. Wire bump **C64M/8**. Names: `mode=inspector`, `enter-inspector` (client, not required on the wire), `leave-inspector` (wire). |

### Inspector UX pins (A1–A18)

Backend pins D5 / D5a / D12 / D16 still hold.

| # | Topic | Decision |
|---|-------|----------|
| **A1** | Slider domain | Oldest retained snapshot -> **live**. Not HST1, not "cycles since boot." |
| **A2** | Grab | Thumb down = **preview only**. One true state stays wherever it was when grabbed. |
| **A3** | Preview | If the frame ring has a still at that time, blit **indexed8** to the CRT. Else **pink** the CRT client area. |
| **A4** | Release | **Land:** load last checkpoint <= that time, paint from the reconstructed C64. Far right = restore **live** (the NOW blob), not the last cadence checkpoint. May sit up to ~one frame *before* a stored still (cadence is cycle-capped, not VBL-locked). |
| **A5** | `[-]` / `[+]` | **One guest VIC-II frame.** `[+]` = re-execute forward to the next `frame_complete` + paint. `[-]` = earlier checkpoint + re-execute to the previous frame + paint (D12). Disable `[-]` at the oldest snapshot, `[+]` at **live**. Disabled while the thumb is down. A step that would pass live **stops at live**. |
| **A6** | F10 / F11 / Shift+F10 / F12 / Shift+F12 | **Re-execute** on the landed C64 — not HST1. **F12** runs forward until a **breakpoint** or **live**, then **stops**. Stay in Inspect. Do **not** leave, do **not** resume the live line, do **not** record more Inspector history. **Shift+F12** = run-to-cursor, still stops at a breakpoint or live. **On stop, present the CRT (A16).** |
| **A7** | Enter Inspect | Requires **checkpoints** only. Film is optional (then most of the bar is pink). HST1 is **not** a gate. Enter **starts at live** (machine is already NOW). Do not land the last cadence checkpoint. Slider at the right. |
| **A8** | Leave Inspect | Restore live **NOW**, still **paused**. |
| **A9** | Mutation | Pokes rejected. F10 / F11 / F12 / `[+]` at live are no-ops or disabled. |
| **A10** | HST1 | **Out of this UI.** It may keep recording in the background. Inspector must not walk HST1 to place the slider or to land. |
| **A11** | Stored film | **Keep the frame ring** as a preview cache. Source of truth is the checkpoint. Native format is **indexed8** (already shipped). |
| **A12** | Thumb follows cycles | Bar is short. Thumb = current machine cycle on oldest->live. After land / `±` / F10-family, if cycles cross a notch, the thumb moves. Not a dual coarseness slider. |
| **A13** | Opt+Left | **Unbound** in Inspect. Not poke-PC, not HST1 run-to, not sealed run-to-cursor. |
| **A14** | Breakpoints | **One list** (existing live / guarded BPs). Opt+B and the Breakpoints tab always edit that list. Inspect **F12** stops on those breakpoints (or live). No Inspect-only copy, no "Run to breakpoint" button, no second bank. |
| **A15** | Promote | **Not this campaign.** |
| **A16** | CRT on stop | Any F10-family / F12 / Pause that leaves the C64 **stopped** must **publish a CRT frame**. Same rule in live and Inspect. c64m has no Apple-style Hardware Override page. **Paint off** (warp, sealed F12 run): dump from current VIC+RAM, then publish. **Otherwise** publish the **beam buffer** so a mid-frame mode switch stays visible. Do not leave the CRT on a vblank captured mid-routine. |
| **A17** | Inspect chrome | While in Inspect, window **headers** use dark cobalt (`nk_rgb(24, 62, 118)` / hover `32, 76, 136` / active `40, 88, 152`) so gray title text still reads. Do **not** tint the window background. |
| **A18** | Inspector tab | Misc -> Inspector is short. **Record off:** checkbox only. **Record on:** **Inspect** (pauses if running) plus History start cycle / Live cycle / Duration (guest seconds = `(live-oldest) / c64_config_clock_hz`). **In Inspect:** **Leave Inspector**, `[-]` slider `[+]`, Current cycle, same three lines. No Pause on this tab. No help dump. Slider grab uses the slider column, not the `[+]` slot (`nk_widget_bounds` **before** `nk_slider_int`). |

### Lessons that still apply

- Join film to checkpoints by **`machine_cycle`**, not frame number. Frame numbers have gaps.
- Address retained frames by **slot / cycle**, not by a dense frame index.
- Do not invent stills to even out the scrubber. Missing film is pink.
- Live `step-instruction` mutates NOW and invalidates peers. Inspect F10 is **sealed re-execute** on the landed machine, clamped to live.
- One skin; live-only verbs (Opt+Left) stay unbound in Inspect rather than guarded at every call site.
- Peer mutation makes cursors stale: `state-changed` -> mark stale -> re-anchor (sessions). Never show silently wrong THEN state.

---

## Shape (target)

```text
                    +-------------------------------------+
  UI (debugger) --->| runtime_client                      |
                    +-----------------+-------------------+
                                      | commands / events
                                      v
                    +-------------------------------------+
                    | runtime worker                      |
                    |  +-------------+  +---------------+ |
                    |  | Machine     |<-| Inspector     | |
                    |  | c64_t       |->| land + execute| |
                    |  +------+------+  +-------^-------+ |
                    |         | record when on         |  |
                    |         v                        |  |
                    |  checkpoints + input log + frames   |
                    +-------------------------------------+
```

**Record path (opt-in, free-run):** cycle-capped checkpoint (`c64_snapshot_save` -> ring slot) + timestamped input log + frame grabs. HST1 may also be on; Inspector does not own it.

**Inspect path (paused, Inspect on):** land = load nearest checkpoint <= target (or restore NOW at live) -> publish. Forward debug = sealed re-execute toward live. Backward = earlier checkpoint + re-execute (D12).

---

## Non-goals (V1 product bar)

- Scrubbing back across a media write — the window is cut there instead (D10)
- Rewinding host file content (D64/G64 bytes on disk)
- Perfect mid-instruction 6510 microstate (focus is an instruction boundary; 1541 mid-micro *does* ride in the snapshot when included)
- Reverse-execution of the 6510
- A write-delta stream (D5a)
- Dual isolated UI vs agent worlds
- Always-on Inspector recording by default
- Inspector-on implying HST1
- Audible audio while Inspecting (SID **state** is correct; output is muted)
- HST1 tape-nav / `SEEK_CYCLE` / `runtime_history_previous` loops for this UI
- A second Inspector breakpoint bank
- A second F7-style debugger shell
- Promote / Branch
- Names: TimeMachine, `timemachine`, `tm_*`, `mode=forensic`, `enter-forensic`

---

## Campaign phases

Implement in order. Each phase ends with: **build + ctest green**, **Landed** in that phase file, and a one-line pointer update here if useful.

| Phase | Doc | Summary |
|-------|-----|---------|
| **I0** | [`I0.md`](I0.md) | Epic contract + opt-in config. No engine. **Landed.** |
| **I1** | [`I1.md`](I1.md) | Checkpoint ring + input log + sealed replay to a **scratch** `c64_t`. **Landed.** |
| **I2** | [`I2.md`](I2.md) | Land / enter / leave into the one true `c64_t`; control honesty (C64M/8). |
| **I3** | [`I3.md`](I3.md) | Misc Inspector tab; film / land / keys / chrome (A1–A18). |
| **I4** | [`I4.md`](I4.md) | Max and warp wipe Record; restore Record on leave into an empty window. |

```text
I0 --> I1 --> I2 --> I3 --> I4
```

---

## Testing strategy (per phase)

| Phase | Prefer |
|-------|--------|
| I0 | ctest: options parse / INI round-trip; off default; arm film on off->on; no HST1 arm |
| I1 | ctest: checkpoint round-trip; sealed re-execution equals golden mem/CPU/VIC/1541 at cycle; one test per seal gate; media-write truncation + housekeeping-does-not |
| I2 | ctest: enter starts at live; leave restores NOW; reject poke; land oldest CP; step in Inspect; mode visible on control |
| I3 | manual playbook + key doc; no flaky UI automation |
| I4 | ctest: wipe-in-max, restore-on-leave, Record-off stays off; warp included |

Keep [`testing.md`](testing.md) gate green every phase.

---

## Risks

| Risk | Mitigation |
|------|------------|
| Checkpoint size vs window depth | c64 snapshots include 1541 GCR when the drive is powered — much larger than a 64K RAM dump. Measure in I1 Landed. Default `inspector_memory_mb=128`. Do not copy a2m's ~800-slot story. |
| Materialize latency | Cadence = one guest frame (`c64_config_cycles_per_frame`: PAL 19656, NTSC 17095). Keep worst-case replay to that span. |
| Leaky seal corrupts the tape | D16 is the top correctness risk. One ctest per gate; recorder counters unchanged across a materialize. |
| Replay divergence | Log keyboard + joystick with cycles. c64m `src/` has no `rand()` today; if one appears on a guest-visible path, seed it in the snapshot. Test: materialize twice -> identical. |
| Window chopped by media writes | Expected (D10). Surface the marker so it reads as a stated rule. |
| Max/warp Duration runaway | I4: wipe on enter max or warp; restore Record on leave into an empty window. Turbo 1 still records. |
| Recording cost when on | Opt-in (D6). Off path stays cheap. |
| One-state coop confusion | `state-changed` + status `mode=inspector` + `leave-inspector` (D18). |

---

## Acceptance bar (Inspector V1 = I0–I4)

- [ ] Opt-in recording; off path stays cheap; does **not** arm HST1
- [ ] Checkpoint + sealed re-execution reconstructs CPU/mem/VIC/CIA/SID/1541 in window
- [ ] Seal proven: materialize leaves HST1, frame ring, watchpoints, audio and host files untouched
- [ ] Inspect replaces the machine with past; views/display update under the head
- [ ] One debugger skin; Misc Inspector tab is the only Inspect entry
- [ ] Film / land / re-execute; drag does not mutate the C64; HST1 unused on this path
- [ ] Read-only pokes; leave restores live NOW paused
- [ ] Guest media write cuts the window, with marker + honest UI text; housekeeping writes do not
- [ ] Cooperative one-state + `state-changed` + control-visible mode/leave (C64M/8)
- [ ] Max and warp wipe Record; leave restores Record into an empty window (I4)

---

## C64 pins (vs the a2m sibling)

| Topic | c64m |
|-------|------|
| Checkpoint | `c64_snapshot_save` / `load` (version 13). Includes 1541 when powered. **Never** flush host media on this path. Paint buffers are **not** in the snapshot (`machine.md`). |
| Cadence | One guest frame of Phi2 cycles from `c64_config_cycles_per_frame`. |
| Duration | Guest seconds = `(live - oldest) / c64_config_clock_hz`. Not Apple `17030*60`. |
| Film | Existing indexed8 frame ring (`copy_by_cycle`). Pink is a UI fill, not a palette index. |
| Input log | `c64_set_key`, `c64_set_joystick`. Paste already goes through keys. No paddles / HostFS. |
| D10 | Successful **guest** 1541 GCR/job write or successful KERNAL SAVE trap. Write-protect refusal does not cut. `sync_dirty` / save-state / eject do not cut. |
| Audio | SID in the snapshot; mute host output under the seal (`c64_set_audio_output_enabled` / do not emit into `audio_out`). |
| Max / warp | Turbo 1 records. Turbo 2 (max) and turbo 3 (warp) wipe Record (I4). c64m has no `history_off_on_max` today. |
| Control bind | `src/main.c` (no `control_dispatch.c`). |
| Wire | **C64M/8**: capability `inspector`; `mode=live\|inspector`; `leave-inspector`; `focus_cycle`; `state-changed` reasons `inspector-enter` / `inspector-land` / `inspector-leave`. Error `read-only-inspector`. |
| Enter on wire | **Not required.** UI uses `runtime_client`. Leave is required on the wire so a socket peer can recover. |

---

## Agent script (when implementation starts)

```text
1. Read agents/README.md, agents/inspector.md (this file), then agents/In.md
   for the phase named in the human brief (only one unless told to continue).
2. Also read deps listed in that phase doc.
3. Implement that phase -> Landed in In.md + one-line status here if useful.
4. Do not start I5 / Promote. Do not build HST1 tape-nav. Do not name anything
   timemachine / forensic / tm_*.
```
