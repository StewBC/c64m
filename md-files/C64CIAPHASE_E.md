# C64 CIA Phase E - CIA #1 Ports: Keyboard, Joystick, and RESTORE Integration

## Mandatory Ground Rules

- The source code is the source of truth. Treat this document as an implementation guide, not as proof that a file, symbol, or behavior currently exists.
- Before editing, inspect the repository and verify the actual file names, structs, functions, tests, and current behavior. Do not hardcode source-file assumptions from this document unless the repo confirms them.
- Implement only this phase. Do not pull later-phase behavior forward unless it is the smallest necessary support for this phase's acceptance criteria.
- Preserve the architecture rules from `AGENTS.md`: frontend must not depend on machine, runtime must not depend on frontend or platform, and live machine state must remain on the runtime thread.
- Preserve the snapshot rule: frontend/debugger views receive copied snapshots or debugger-safe peeks only; no live machine pointers may cross threads.
- At the end of the phase, update `md-files/STATUS.md` succinctly with the implemented behavior, tests/diagnostics added, known limitations, and any deferred items.

## Required Reading

1. `AGENTS.md`
2. `md-files/STATUS.md`
3. `C64MCIA.md`
4. This phase document
5. Relevant source and tests discovered in the repository

## Repository Inspection Rule

Use repository search to locate the actual CIA, C64 bus, runtime, debugger snapshot, keyboard/joystick, IEC, CPU interrupt, and test code as needed. Prefer names discovered in source over names from planning documents. If the repo disagrees with this document, trust the repo, then update this document or `md-files/STATUS.md` only if the discrepancy is material to the phase handoff.

## Recommended CIA Implementation Sequence

From an implementation-dependency standpoint, use this sequence unless source inspection proves a better local order:

```text
A, B, D, C, E, F, G, H, I, J, K
```

This filename is alphabetical by phase, but the practical execution sequence intentionally places D before C so interrupt behavior is stable before expanding timer mode complexity.

## Goal

Complete CIA #1 port behavior as wired in the C64: keyboard matrix scanning, joystick ports, shared active-low line behavior, and RESTORE/NMI-related input routing.

## Implementation Order Position

Run after core register/timer/interrupt phases A, B, D, and ideally after C if PB6/PB7 port interactions are implemented. E should precede CIA #2 IEC work if keyboard/joystick correctness is the next user-visible priority.

## Scope

- PRA/PRB reads combine output latch, DDR direction, and external line states.
- DDRA/DDRB select input vs output per bit.
- Output latch state is preserved even when a bit is configured as input.
- Input bits read high when un-driven.
- Keyboard matrix affects CIA #1 port reads through an active-low line-pull model.
- Multiple simultaneous keys must combine correctly.
- Shift, Commodore, Control, cursor keys, RUN/STOP, and ordinary typing remain representable.
- Joystick port 1 and port 2 directional/fire inputs share CIA #1 lines with the keyboard matrix.
- Keyboard and joystick pulls combine electrically: either source can pull a shared line low.
- RESTORE remains outside the ordinary keyboard matrix and enters the CPU NMI path.

## Out Of Scope

- CIA #2 IEC port behavior.
- Light pen beyond any existing fire-button wiring if present.
- Cycle-perfect keyboard/joystick sampling.
- Host frontend key mapping redesign unless source inspection shows it is required for the machine-level model.

## Repo Inspection Tasks

- Locate machine-owned keyboard matrix state.
- Locate runtime copied key down/up command handling.
- Locate frontend key mapping and RESTORE command path.
- Locate CIA #1 PRA/PRB read code.
- Locate existing deterministic no-key and BASIC typing tests.
- Locate or add a machine-owned joystick input model if none exists.

## Implementation Guidance

1. Keep generic CIA port latch/DDR behavior separate from C64-specific external wiring.
2. Put C64-specific keyboard/joystick line combination in machine/bus wiring or another architecture-appropriate layer, not in frontend.
3. Model active-low inputs: released/un-driven lines read high; pressed directions/buttons/keys pull low.
4. Preserve copied runtime commands from frontend to runtime. Do not pass SDL events to machine.
5. Ensure RESTORE remains a separate machine-level NMI input, not a fake matrix key.
6. Add joystick state using the same copied-command discipline as keyboard if frontend input is involved.
7. Avoid speculative support for device classes not needed by tests.

## Required Tests And Diagnostics

- Direct matrix diagnostic that drives CIA #1 port output rows/columns and reads expected active-low key closures.
- Multiple-key diagnostic for combined line pulls.
- Existing BASIC typing and paste tests.
- Shift, Commodore, Control, cursor, RUN/STOP, HOME/CLR HOME, DEL/INST regressions.
- Joystick port 1 directional/fire bit tests.
- Joystick port 2 directional/fire bit tests.
- Keyboard plus joystick shared-line combination test.
- RESTORE still enters CPU NMI path and does not appear as an ordinary matrix key.

## Acceptance Criteria

- Existing BASIC typing, cursor, Shift, Commodore, Control, RUN/STOP, and paste tests continue to pass.
- A direct keyboard matrix diagnostic sees active-low key closures through CIA #1 ports.
- Joystick port 1 and port 2 diagnostics read correct direction/fire bits.
- Keyboard and joystick inputs combine electrically: either source can pull a shared line low.
- RESTORE enters the CPU NMI path without corrupting ordinary keyboard matrix state.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase E complete, CIA #1 port model, keyboard matrix behavior, joystick support status, RESTORE path status, tests added, and any deferred light-pen or edge-timing limitations.
