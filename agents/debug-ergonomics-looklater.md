# Debug / agent ergonomics — look-later (not active work)

**Status:** note only · **Not** an epic · **Not** a commitment to implement  
**Origin:** 2026-08 ranked list after comparing sibling **a2m-v2** product remote
debug (A2M/6 + coop tools) to AltirraBridge. a2m captured the same ranking in
`../a2m-v2/agents/backlog.md` under **E1–E4**.

This file exists so a future session can **give these ideas a real look** without
re-deriving them from chat. It is **not** a claim that c64m is missing all four,
or that a2m’s ranking is correct for c64m as-is.

---

## BUT CHECK (mandatory before any work)

**Do not implement from this note alone.** Before treating any item as a gap,
a future agent **must** verify against **current source + handoffs** in *this*
tree. Handoffs here go stale; the C source and tests are authoritative
([`README.md`](README.md)).

For each ranked item:

1. **Search product UI** (`src/frontend/`) and **control wire**
   ([`control-port.md`](control-port.md), `src/control/`) for an existing verb
   or equivalent workflow.  
2. **Try the agent path** with `tools/c64_control_client.py` / `coop_watch` —
   client-side composition may already be “good enough.”  
3. **Diff with a2m only as inspiration**, not as a port checklist. C64 media
   (D64/G64/CRT), VIC/CIA rings, drive-cpu, and `step-frame` change the story.  
4. **If it already exists**, delete or rewrite that row here and (if needed)
   leave a one-line “already landed: …” pointer. Do not open an epic for
   vapour.  
5. **If partially present**, document what remains (e.g. path save-state exists
   but named slots do not) before coding.

A wrong “we need X” from this file is worse than no file. **BUT CHECK first.**

---

## Ranked list (same order as a2m E1–E4)

When time opens and **BUT CHECK** is done, consider top-down. Do **not** chase
Altirra’s profiler/symbols suite as parity.

| Rank | ID | Item | Suggested surface (if still a gap) |
|------|----|------|--------------------------------------|
| 1 | **E1** | Memory workshop ops (fill / move / named range backup + diff) | **UI first**, wire second if cheap |
| 2 | **E2** | Live media remount / session reconfig | UI + wire; session pain not “every INI key” |
| 3 | **E3** | Machine-state checkpoint / rewind | Not history; not mem-range backup — full restore |
| 4 | **E4** | Deterministic frame gate + richer HW snapshots | Goldens / deep RE; often lower value if already covered |

### E1 — Memory workshop

**Hypothesis (unverified):** product has memory *access* (mem view, `get-memory`
/ `set-memory`, flight recorder / write history) but may lack classic **monitor**
verbs humans reach for daily.

| Op | Use |
|----|-----|
| **Fill** | Sentinels, clear buffers, poison free space |
| **Move / copy** | Relocate blocks, overlap-safe table patches |
| **Named range backup → later diff** | Structure-level before/after without history-find |

**Not the same as history:** history answers *who wrote / which PC*; range
snapshot answers *what the whole region looks like now vs then*.

**BUT CHECK:** any UI fill/move; whether `set-memory` length limits make fill
painful; whether coop_watch / client already diffs regions.

### E2 — Live media remount / session reconfig

**Hypothesis:** cold-start CLI/INI covers most config; pain is mid-session swap
(D64/G64/CRT, drive power, writable flag, PRG re-inject) without relaunch.
Altirra-style “wire config” envy is usually this, not a giant key matrix.

**BUT CHECK:** `mount-d64` / `unmount-disk` / power-drive / frontend Configure
paths — what is live today vs “needs relaunch”? See
[`disk-iec1541.md`](disk-iec1541.md) and [`control-port.md`](control-port.md).
c64m may already be ahead of a2m here.

### E3 — Machine-state checkpoint / rewind

**Hypothesis:** history + mem backup do **not** restore the machine. Probe path
wants: save slot → poke / run → if dead, **rewind**, try next hypothesis.

**BUT CHECK:** path `save-state` / `load-state`, snapshot version rules
([`machine.md`](machine.md)), pause preservation, whether named/in-memory slots
or quicksave chords exist ([`frontend-debugger.md`](frontend-debugger.md)).
Do not reinvent if path + UI load is already the product answer.

### E4 — Deterministic frame gate + richer HW snapshots

**Hypothesis (often weaker on c64m):** goldens want “advance exactly N frames”;
deep RE wants chip dumps.

**BUT CHECK carefully — c64m may already win much of this:**

| Candidate | Where to look |
|-----------|----------------|
| Frame advance | `step-frame`, `wait-frame`, run-to-raster in [`control-port.md`](control-port.md) |
| Frame scrub | frame ring ([`frame-ring-plan.md`](frame-ring-plan.md)) |
| VIC line state | VIC ring |
| Chip snapshots | `get-vic` / `get-cia` / drive-cpu / debug-memory |

If those cover day-to-day and agent goldens, **E4 may be “done enough”** —
record that and stop. Only residual gaps (e.g. Altirra-style `FRAME n` gate
semantics, or a missing SID dump) deserve work.

---

## Explicit non-goals for this note

| Item | Why |
|------|-----|
| Port Altirra profiler / verifier / symbols as a campaign | Optional RE suite; not the ranked list |
| Replace HST1-style history with a tail-only dump | Searchable recorder is a product strength |
| “Wire every config key” | Session/media (E2) is the real pain if any |
| Blind port of a2m E1–E4 code | Different machine, media, and existing wire depth |

---

## Sibling pointer

a2m-v2 ranking + slightly more product-specific notes:
`../a2m-v2/agents/backlog.md` → section **P2 — Debug / agent ergonomics**.
When either tree lands an item, a short cross-note helps the other — still
**BUT CHECK** before copying behaviour.
