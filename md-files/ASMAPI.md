# Assembler UI API Notes

This is a short handoff note for building a debugger/UI entry point for the
c64m assembler.

## What the Assembler Needs

At minimum, assembly needs:

- a source file path
- a load/assembly address

The runtime helper API also accepts:

- an optional `symbol_table *` for importing labels
- an optional `source_name` used to replace old assembler symbols from the same source
- a caller-owned error buffer

Current public helper:

```c
bool c64_assemble_file(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size);
```

`runtime_assemble_file()` has the same signature and currently just wraps
`c64_assemble_file()`.

## Direct Helper Flow

Use this path when the caller already owns or can safely access the `c64_t`.

1. Pick a source path, for example from a file picker.
2. Pick an address, for example `$0801`.
3. Call `c64_assemble_file(...)`.
4. On success, bytes have been written to C64 RAM through `c64_debug_write_ram`.
5. On failure, show the returned `error` string.

The assembler does not call back into the UI for diagnostics. It writes
diagnostics into its internal `ERRORLOG` during assembly, and
`c64_assemble_file()` formats those log entries into the caller-provided
`error` buffer before returning `false`.

There is no persistent assembler error log exposed for UI querying after the
call. The UI should copy or display the `error` buffer immediately.

## Runtime Client Flow

The runtime client has an asynchronous command for UI code:

```c
bool runtime_client_assemble_file(
    runtime_client *client,
    const char *path,
    uint16_t address);
```

This queues `RUNTIME_COMMAND_ASSEMBLE_FILE`. The runtime thread currently
requires the machine to be paused before assembling. If the runtime is not
paused, it publishes a `RUNTIME_EVENT_ERROR`.

Expected events:

- `RUNTIME_EVENT_ERROR`: assembly failed, invalid state, or invalid input.
  Show `event.data.error.message`.
- `RUNTIME_EVENT_ASSEMBLE_COMPLETE`: assembly succeeded.
  The event includes `event.data.assemble.address` and
  `event.data.assemble.path`.
- After success, the runtime also publishes a RAM memory snapshot starting at
  the assembly address and a machine-state update.

The runtime event queue is the UI-facing error channel for this path. The
assembler still does not call the UI directly.

## Symbols

`c64_assemble_file()` can import labels when passed a non-null `symbol_table *`.
It removes previous `SYMBOL_SOURCE_ASSEMBLER` symbols for the same `source_name`
and then imports current address labels.

The assembler symbol walker exports resolved address labels, including scoped
names such as:

```text
Outer::Inner::label
```

Macro-local generated symbols are skipped.

Current caveat: the queued runtime command path calls `runtime_assemble_file`
with `symbols == NULL`, so it assembles bytes into RAM but does not yet import
symbols through that path. A future UI/debugger integration should either pass
the debugger symbol table through the runtime command path or use the direct
helper where symbol-table ownership is clear.

## Practical UI Checklist

- Provide a source file picker.
- Provide an address field with hex input.
- Pause the runtime before sending `runtime_client_assemble_file()`.
- Disable or show a busy state until an assemble-complete or error event arrives.
- On `RUNTIME_EVENT_ERROR`, show `event.data.error.message`.
- On `RUNTIME_EVENT_ASSEMBLE_COMPLETE`, refresh memory/disassembly around the
  requested address.
- If symbols are wired in, refresh any disassembly label resolver after a
  successful assembly.
