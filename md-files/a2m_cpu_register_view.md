# a2m CPU/register view reference

This is a behavioral reference for Phase 12 planning only. Do not reuse the a2m implementation structure: it lets Nuklear UI code read and mutate the live machine/runtime directly, which is not valid for c64m.

## Fields shown

- Window title: `CPU`.
- First row fields:
  - `PC`: 16-bit program counter.
  - `SP`: 16-bit stack pointer in a2m. It is formatted as four hex digits.
  - `A`: accumulator, 8-bit.
  - `X`: X register, 8-bit.
  - `Y`: Y register, 8-bit.
- Second row fields:
  - Processor status flags in the order `N V E B D I Z C`.
  - Each flag has a one-character label and a one-character editable value.

## Update behavior

- The view is refreshed from CPU state when `unk_cpu_show(..., dirty)` is called with `dirty != 0`.
- Refresh overwrites the local text buffers for all registers and flags.
- Edits are committed only when the Nuklear edit widget reports `NK_EDIT_COMMITED`, which is normally Enter in this UI.
- On commit, the edit field is nul-terminated, the active edit control is cleared, the text is parsed, and the runtime setter is called.
- In a2m, edits call live runtime/machine setters immediately. In c64m this must become frontend command dispatch through `runtime_client`, with the frontend later reflecting copied CPU snapshots.

## Formatting

- `PC`: uppercase hex, four digits, `%04X`.
- `SP`: uppercase hex, four digits, `%04X`.
- `A`, `X`, `Y`: uppercase hex, two digits, `%02X`.
- Flag values: decimal `0` or `1`.
- Register edit filters accept hex input.
- Flag edit filters accept binary input.
- Register input buffers allow enough text for four hex digits plus terminator for `PC`/`SP`, and two hex digits plus terminator for `A`/`X`/`Y`.

## Flags display

- Flags are displayed most-significant bit first.
- Label string is `NVEBDIZC`.
- For display slot `i`, the bit tested is `1 << (7 - i)`.
- Editing a flag clears that bit in the current status register and ORs in the parsed value shifted to `7 - i`.
- The `E` flag is Apple II specific. For c64m, the equivalent 6510/6502 status display should normally be `N V - B D I Z C`, or `N V U B D I Z C` if the unused bit is named explicitly.

## Interactions and shortcuts

- No custom keyboard shortcut is handled by the CPU/register view.
- Mouse or keyboard focus inside an edit field follows Nuklear text edit behavior.
- `Enter` in a register edit field commits the field:
  - View: CPU/register.
  - Changes: sends a register-set request for `PC`, `SP`, `A`, `X`, or `Y`.
  - Edge cases: invalid or empty input falls through `sscanf` behavior in a2m; c64m should validate or leave unchanged on parse failure.
- `Enter` in a flag edit field commits the field:
  - View: CPU/register.
  - Changes: sends a status-register update with one bit replaced.
  - Edge cases: a2m parses a decimal integer and shifts it into the target bit, so values other than `0`/`1` are not robustly guarded beyond the input filter. c64m should clamp or reject non-binary values.

## Recommended c64m design

- Keep the CPU/register view entirely in `frontend`.
- The view renders from a copied CPU snapshot received from `runtime_client`.
- Runtime owns the live `c64` machine and is the only layer that may mutate CPU state.
- Register and flag edits should enqueue runtime commands such as set-PC, set-SP, set-register, or set-status. The UI should keep a pending edit buffer separate from the last snapshot so refreshes do not destroy in-progress typing.
- The frontend should not include disassembly or CPU truth logic beyond formatting copied values.
- Any instruction decode needed near the CPU view should call a tool-layer disassembler using bytes from copied memory snapshots.
