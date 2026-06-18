# C64 CIA Phase C - Timer Control Modes, PB Output, and Cascade Sources

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

Complete the timer mode matrix controlled by CRA and CRB, including CNT input modes, Timer B cascade modes, PB6/PB7 timer output behavior, pulse/toggle behavior, and timer-output interaction with port B.

## Implementation Order Position

Run after Phases A, B, and D if interrupt behavior is still unstable. From an implementation standpoint, D can come before C because timer mode tests are easier to interpret once ICR and IRQ/NMI behavior are reliable.

## Scope

- CRA bit 5 selects Timer A input source: Phi2 or CNT.
- CRB bits 5-6 select Timer B input source.
- Timer B can count Phi2, CNT, Timer A underflows, and Timer A underflows while CNT is active if supported by the selected CIA model.
- CRA bit 1 enables Timer A output on PB6.
- CRA bit 2 selects PB6 pulse vs toggle output.
- CRB bit 1 enables Timer B output on PB7.
- CRB bit 2 selects PB7 pulse vs toggle output.
- Timer outputs interact with normal Port B read behavior according to the selected CIA reference.
- Pulse mode produces a one-cycle observable output on underflow.
- Toggle mode toggles output on underflow.

## Out Of Scope

- Full TOD implementation except CRB bit 7 documentation.
- Serial shift behavior driven by Timer A.
- FLAG and PC handshake behavior.
- Cycle-race polish beyond deterministic functional timing.

## Control Bits To Implement Or Verify

```text
CRA bit 0: Timer A start
CRA bit 1: Timer A PB6 output enable
CRA bit 2: Timer A PB6 pulse/toggle select
CRA bit 3: Timer A run mode
CRA bit 4: Timer A force-load strobe
CRA bit 5: Timer A input source, Phi2 or CNT
CRA bit 6: serial direction/control, document/defer serial behavior to Phase H
CRA bit 7: TOD clock source policy, document/defer TOD behavior to Phase G

CRB bit 0: Timer B start
CRB bit 1: Timer B PB7 output enable
CRB bit 2: Timer B PB7 pulse/toggle select
CRB bit 3: Timer B run mode
CRB bit 4: Timer B force-load strobe
CRB bits 5-6: Timer B input source selection
CRB bit 7: TOD alarm write select, document/defer TOD behavior to Phase G
```

## Repo Inspection Tasks

- Locate Port B read/write and DDR behavior.
- Locate timer stepping and underflow notification code.
- Locate any existing external CNT/SP line representation.
- Locate keyboard/joystick code that reads CIA #1 ports, because PB6/PB7 output must not break it.
- Locate VIC-bank code that depends on CIA #2 ports, to ensure generic CIA port changes do not corrupt CIA #2 behavior.

## Implementation Guidance

1. Add a small, explicit timer input-source abstraction only if source inspection shows it simplifies current code; do not build a speculative general signal graph.
2. Make Timer A underflow events available to Timer B cascade without exposing live machine pointers across architectural boundaries.
3. Model CNT as an input event source on the generic CIA if source structure supports it; otherwise define the smallest C64-machine-facing hook needed by later serial/IEC work.
4. Define PB6/PB7 output state inside CIA timer/port state.
5. Merge timer-driven PB6/PB7 output with normal PRB/DDR behavior in the read path.
6. Keep pulse/toggle timing deterministic and covered by tests; defer pin-perfect races to Phase J.

## Required Tests And Diagnostics

- Timer A Phi2 vs CNT source selection.
- Timer B Phi2 source.
- Timer B CNT source.
- Timer B counts Timer A underflows.
- Timer B combined Timer A-underflow-and-CNT mode if selected model supports it.
- PB6 pulse mode is observable for one cycle or the project's defined pulse duration.
- PB7 pulse mode is observable.
- PB6/PB7 toggle mode changes state on repeated underflows.
- Port B ordinary behavior is restored when timer output enable is clear.
- Regression for keyboard, joystick placeholders if any, VIC-bank, timer, and interrupt tests.

## Acceptance Criteria

- Timer A decrements from Phi2 and CNT sources according to CRA.
- Timer B decrements from Phi2, CNT, Timer A underflow, and combined modes according to CRB.
- PB6/PB7 reflect timer output when enabled and ordinary port behavior when disabled.
- Pulse mode produces a one-cycle observable output or a documented project-equivalent pulse.
- Toggle mode toggles on repeated underflows.
- Timer cascade tests can use Timer A underflow to clock Timer B.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase C complete, implemented CRA/CRB mode matrix, PB6/PB7 policy, CNT/cascade policy, tests added, and any cycle-level or variant limitations deferred to Phase J.
