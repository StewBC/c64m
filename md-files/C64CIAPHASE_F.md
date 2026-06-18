# C64 CIA Phase F - CIA #2 Ports: VIC Bank and IEC Serial Bus Integration

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

Complete CIA #2 port behavior as wired in the C64: VIC bank selection from effective port A output levels, IEC serial bus control/sense lines, and CIA #2 NMI-capable interrupt behavior.

## Implementation Order Position

Run after A, B, D, and preferably after the generic port behavior work used by Phase E. This phase depends on correct PRA/DDRA behavior and should preserve existing VIC-bank rendering.

## Scope

- CIA #2 Port A bits 1-0 select the VIC-II bank through inverted C64 wiring.
- VIC bank selection derives from effective CIA #2 output level, not merely raw writes.
- DDRA and external line state influence effective output/read levels.
- IEC ATN, CLK, DATA, and sense lines are readable/drivable through CIA #2 port wiring.
- IEC lines use open-collector/open-drain style behavior: any attached source can pull low; released lines read high.
- CIA #2 enabled interrupt sources route to CPU NMI using Phase D ICR behavior.

## Out Of Scope

- Disk-drive emulation.
- Full IEC protocol state machine.
- Serial data register shift behavior; that belongs to Phase H.
- Cycle-perfect IEC timing.
- RS-232 completeness unless already present and required by tests.

## Repo Inspection Tasks

- Locate VIC bank selection code and all VIC memory read paths.
- Locate CIA #2 port A read/write and DDR behavior.
- Locate any existing IEC bus representation or stubs.
- Locate CPU NMI path and CIA #2 interrupt-output wiring from Phase D.
- Locate tests that validate bank-aware character, bitmap, sprite, and screen rendering.

## Implementation Guidance

1. Keep the generic CIA layer generic; C64-specific CIA #2 pin wiring should live in machine/bus-level code if the repo structure supports that.
2. Compute VIC bank from effective CIA #2 PA0/PA1 output levels using the C64's inverted mapping.
3. Preserve current default VIC bank behavior after reset.
4. Introduce the smallest useful IEC bus object/state if none exists. Model line release vs pull-low; avoid adding drive emulation.
5. Ensure CPU-visible port reads reflect combined CIA output, DDR, and external IEC line state.
6. Do not break snapshot rendering, live rendering, Bad Line fetches, or sprite fetches that read through VIC bank base.

## Required Tests And Diagnostics

- Existing VIC bank rendering tests continue to pass.
- DDRA changes affect VIC bank only through effective output state.
- Writing CIA #2 PA0/PA1 changes VIC bank according to inverted wiring.
- IEC line release reads high.
- CIA pulling an IEC line low reads low.
- A second attached source pulling an IEC line low keeps it low after CIA release, if an external source abstraction is added.
- CIA #2 enabled timer interrupt still produces CPU NMI after port changes.

## Acceptance Criteria

- Existing VIC bank tests continue to pass.
- VIC bank selection changes according to effective CIA #2 port A output bits 1-0.
- IEC lines can be read and driven through CIA #2 port registers with active-low behavior.
- Releasing an IEC line lets it return high unless another attached source pulls it low.
- CIA #2 enabled interrupt sources can produce CPU NMI entries.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase F complete, effective CIA #2 VIC-bank policy, IEC line model, NMI regression status, tests added, and explicit note that disk-drive/protocol emulation remains deferred unless implemented elsewhere.
