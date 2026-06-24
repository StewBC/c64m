# Implementation Guide: Paste Parser One-Shot Modifiers and Wait Tokens

## Reading order

Before implementing, read the repository docs in the project-required order, starting with:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `docs/status/README.md`
5. `docs/status/FRONTEND_DEBUGGER.md`

Then inspect the current paste parser and paste event sequencer implementation.

## Scope

This change applies to the existing input-encoding parser and matrix/event paste path.

Do not change the `use_buffer` path.

Do not introduce an alternate compact syntax. Keep the existing `\[...]` named-token syntax.

## Required behavior changes

### 1. Bare modifier tokens become one-shot modifiers

The following bare named modifier tokens should no longer mean "press and release this modifier by itself":

```text
\[SHIFT]
\[CTRL]
\[CBM]
\[RUNSTOP]
```

Their short aliases should behave the same way:

```text
\[SH]
\[CT]
\[CB]
\[RS]
```

Important: the settled aliases are:

```text
RE = RESTORE
RS = RUNSTOP
```

A bare modifier means:

1. assert the modifier immediately,
2. remember it as a one-shot modifier,
3. continue parsing/executing subsequent events,
4. when the next non-modifier keypress completes, release only the one-shot modifiers.

Example:

```text
\[CTRL]\[SHIFT]S
```

should type Ctrl+Shift+S, then release SHIFT and CTRL.

### 2. Keep explicit hold/release unchanged

The existing `+` and `-` forms remain as-is.

Examples:

```text
\[CTRL+]
\[CTRL-]
\[SHIFT+]
\[SHIFT-]
```

These explicitly assert and deassert the modifier.

They are still useful for longer held-key sequences.

### 3. Explicit modifier holds win over one-shot cleanup

Track explicit-held modifiers separately from one-shot modifiers.

Recommended model:

```text
explicit_down[modifier]
oneshot_down[modifier]
```

Rules:

```text
\[CTRL+]  -> assert CTRL; explicit_down[CTRL] = true; oneshot_down[CTRL] = false
\[CTRL-]  -> deassert CTRL; explicit_down[CTRL] = false; oneshot_down[CTRL] = false
\[CTRL]   -> if CTRL is not explicitly down and not already one-shot down:
                 assert CTRL
                 oneshot_down[CTRL] = true
             otherwise:
                 do nothing
normal keypress completes:
             release only modifiers marked in oneshot_down
             clear oneshot_down
```

This means:

```text
\[CTRL+]\[CTRL]SA\[CTRL-]
```

should behave as:

```text
\[CTRL+]   -> CTRL held explicitly
\[CTRL]    -> ignored as redundant one-shot request
S          -> CTRL+S
A          -> CTRL+A
\[CTRL-]   -> CTRL released
```

The bare `\[CTRL]` must not cause CTRL to be released after `S`, because CTRL was explicitly held.

### 4. Synthetic shifted keys must respect modifier ownership

Some named keys are implemented as SHIFT plus another physical key, for example F2, F4, F6, F8, cursor-left, cursor-up, and PI.

If the sequencer temporarily asserts SHIFT for one of these synthetic shifted keys, it must not release SHIFT if SHIFT was already explicitly held or one-shot held by the user.

Use the same ownership idea as above: only release a modifier if this operation asserted it temporarily, or if it is part of one-shot cleanup.

### 5. RESTORE remains special

RESTORE is still an NMI event, not a matrix key.

Use:

```text
\[RESTORE]
\[RE]
```

Do not treat RESTORE as a one-shot modifier.

RUNSTOP is a modifier for this feature:

```text
\[RUNSTOP]
\[RS]
```

This allows shorthand sequences involving RUNSTOP plus RESTORE, while RESTORE itself remains a pulse/NMI event.

### 6. Add wait tokens

Add two named wait forms:

```text
\[W:N]
\[WAIT:N]
```

`N` is an integer multiplier.

The wait duration is:

```text
N * normal_keypress_full_time
```

where `normal_keypress_full_time` means the normal press/hold plus normal gap timing.

Examples:

```text
S\[W:2]A
S\[WAIT:2]A
```

These type `S`, wait for two normal keypress durations, then type `A`.

`N = 0` is valid and means no-op.

Invalid or missing values should be parse errors, except for `0`, which is explicitly accepted as no-op.

### 7. Parser/event model

Add a wait event type to the existing paste event model.

Suggested event:

```c
PASTE_EV_WAIT
```

with a multiplier/count field.

Do not add a second syntax family such as `\p...` or `\s...`.

Do not make token parsing fuzzy. Named keys should remain exact matches against canonical names or aliases.

### 8. Cleanup behavior

When a paste sequence completes, is cancelled, errors during startup, or the runtime resets paste state, release any keys that were asserted by the paste sequencer.

This includes:

```text
explicit_down modifiers
oneshot_down modifiers
temporary synthetic modifiers
any asserted regular keys
```

Avoid stuck modifier keys.

## Documentation updates

### `manual/manual.md`

Update `manual/manual.md` in the existing style and in the appropriate input-encoding / paste / breakpoint Type section.

Document:

1. Bare modifier tokens are one-shot modifiers.
2. Explicit `+` and `-` hold/release tokens still work.
3. The aliases are:

   * `RE` = RESTORE
   * `RS` = RUNSTOP
4. Examples:

```text
\[CT]S
\[CT]\[SH]S
\[CTRL+]\[CTRL]SA\[CTRL-]
\[RS]\[RE]
S\[W:2]A
S\[WAIT:2]A
```

Explain that `\[CTRL+]\[CTRL]SA\[CTRL-]` keeps CTRL held for both `S` and `A`; the middle bare `\[CTRL]` is redundant because CTRL is already explicitly held.

Explain that `\[W:0]` is accepted and does nothing.

### Status docs

Update status after implementation.

Follow the repo rule from `AGENTS.md`: keep `STATUS.md` short and only add top-level facts if they affect routing or current baseline.

Update the relevant detailed component handoff, expected to be:

```text
docs/status/FRONTEND_DEBUGGER.md
```

Mention that the input-encoding parser now supports:

```text
- one-shot bare modifiers
- explicit modifier hold/release preservation
- RESTORE alias RE and RUNSTOP alias RS
- wait tokens \[W:N] and \[WAIT:N]
- \[W:0] as no-op
```

If `STATUS.md` already has a recent high-value handoff note for Type/input encoding, update that short note only if needed to keep the current baseline accurate.

## Tests / validation

Add or update tests for the parser and sequencer behavior.

Required cases:

```text
\[CT]S
```

Expected: CTRL asserted, S pressed/released, CTRL released.

```text
\[CT]\[SH]S
```

Expected: CTRL and SHIFT asserted, S pressed/released, SHIFT and CTRL released.

```text
\[CTRL+]\[CTRL]SA\[CTRL-]
```

Expected: CTRL remains held for both S and A, then releases only at `\[CTRL-]`.

```text
\[RS]\[RE]
```

Expected: RUNSTOP one-shot applies to RESTORE/NMI, then RUNSTOP is released.

```text
S\[W:2]A
```

Expected: S, wait for two normal full keypress durations, A.

```text
S\[W:0]A
```

Expected: S, no-op wait, A.

Also include at least one case where a synthetic shifted key is used while SHIFT is already held, to confirm the synthetic key path does not release user-held SHIFT.
