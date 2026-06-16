# Phase 13: Breakpoints

## Required Reading

Before implementing Phase 13, read:

1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64PHASE12.md

---

## Goal

Enhance the current execute-breakpoint-only system with a general runtime-owned breakpoint and watchpoint system.

Phase 13 also introduces the first modal dialog system through a breakpoint editor dialog.

The runtime remains the sole owner of breakpoint state, hit counts, actions, and evaluation.

Frontend code edits copied breakpoint definitions only and sends commands through runtime_client.

---

## Architecture Rules

Do not violate these rules:

* frontend must not include or call machine directly
* frontend must communicate through runtime_client
* runtime owns all breakpoint definitions
* runtime owns all hit counters
* runtime owns all action execution
* runtime owns breakpoint evaluation
* frontend renders copied breakpoint snapshots only
* frontend may own dialog visibility and temporary edit state
* machine must not know about debugger UI concepts
* breakpoint persistence must not introduce frontend-to-machine dependencies

---

## Breakpoint Model

A breakpoint definition contains:

```text
runtime id
enabled
start address
optional end address
access mask
mapping filter
action set
optional counter
optional reset counter
```

### Address

A breakpoint may target:

```text
single address
address range
```

Examples:

```text
$C000
$D000-$DFFF
```

Address math is 16-bit.

Ranges are inclusive.

### Identity

Duplicate breakpoints are allowed at the same address or address range.

Runtime assigns each breakpoint a stable runtime ID.

UI operations use runtime IDs.

INI keys are persistence labels only and are not runtime identity.

On load, runtime assigns fresh IDs.

---

## Access Types

Supported access types:

```text
Execute
Read
Write
```

The UI presents these as independent checkboxes.

Expected usage:

```text
Execute only
Read only
Write only
Read + Write
Execute + Read
Execute + Write
Execute + Read + Write
```

Execute remains the default for newly-created breakpoints.

For INI parsing:

```text
access
```

may be accepted as shorthand for:

```text
read,write
```

but runtime stores individual flags only.

---

## Mapping Filters

Supported mapping filters:

```text
Map
ROM
RAM
```

### Map

Default mode.

The breakpoint evaluates against whatever the CPU sees at the target address.

### ROM

The breakpoint only activates if ROM is currently visible at the target address.

### RAM

The breakpoint only activates if RAM is currently visible at the target address.

The runtime owns all mapping evaluation.

Frontend never evaluates mapping state.

Runtime should evaluate ROM/RAM filters using the same machine-side address-decoding logic used for CPU reads and writes.

Do not duplicate C64 banking rules in runtime.

If no suitable helper exists, add a small machine-facing helper used only by runtime, for example:

```c
c64_memory_visibility c64_memory_visibility_at(c64 *machine, uint16_t address);
```

With values like:

```text
C64_MEMORY_VISIBILITY_RAM
C64_MEMORY_VISIBILITY_ROM
C64_MEMORY_VISIBILITY_IO
```

Phase 13 filter policy:

```text
map = always eligible
rom = visible kind is ROM
ram = visible kind is RAM
io = matches map only
```

Do not make IO match ROM or RAM unless a later phase adds an explicit IO filter.

---

## Actions

A breakpoint may contain multiple actions.

Actions are not mutually exclusive.

Examples:

```text
Break + Tron
Break + Fast
Tron only
Swap + Type
```

Supported actions:

```text
Break
Fast
Slow
Tron
Troff
Type
Swap
```

When a breakpoint has multiple actions, runtime executes them in this order:

```text
Break
Fast / Slow
Tron / Troff
Swap
Type
```

Break runs first so the debugger shows machine state before later actions change speed, tracing, disk state, or typed input.

### Break

Pause emulator execution.

### Fast

Switch emulator speed to maximum turbo mode.

### Slow

Switch emulator speed to 1 MHz mode.

### Tron

Enable tracing.

Phase 13 implementation may be a no-op.

### Troff

Disable tracing.

Phase 13 implementation may be a no-op.

### Type

Inject text into the emulated machine.

Phase 13 implementation may be a no-op.

### Swap

Future disk-swap action.

Phase 13 implementation may be a no-op.

---

## Counters

Breakpoints may optionally use counters.

Fields:

```text
use counter
initial count
reset count
```

Behavior:

```text
counter reaches zero
-> actions execute

counter reloads from reset count
```

If reset count is omitted, runtime uses the initial count as the reset count.

Counter values use these validation rules:

```text
count=0 is valid and triggers on the first matching hit
reset=0 is valid and triggers on every matching hit after the first trigger
negative counts are invalid
non-numeric counts are invalid
```

An invalid counter makes that breakpoint invalid.

Invalid breakpoints are ignored with a warning.

Disabled breakpoints do not decrement counters.

Example:

```text
initial = 10
reset = 2
```

The breakpoint triggers after 10 hits.

Afterward it triggers every second hit.

Counter ownership belongs entirely to runtime.

---

## Runtime Ownership

Runtime owns:

```text
breakpoint creation
breakpoint deletion
enable/disable state
hit counters
mapping evaluation
access evaluation
action execution
INI persistence data
```

Frontend owns:

```text
dialog visibility
dialog widgets
temporary edit buffers
selection state
```

---

## Breakpoint Editor Dialog

This is the first modal dialog implementation in c64m.

Purpose:

```text
Create breakpoint
Edit breakpoint
Duplicate breakpoint
```

### Layout

Sections:

```text
Access
Address
Mapping
Counter
Actions
Buttons
```

The image 'md-files/breakpoint-edit.png' is a behavioral and layout reference only.

Do not copy Apple II-specific controls or terminology.

Do not copy code structure from any other project.

Use the image only to understand:
- dialog density
- grouping of controls
- approximate sizing
- visual hierarchy

The resulting dialog must be C64-specific and implement only the controls described in C64PHASE13.md.

### Access Section

Controls:

```text
[ ] Execute
[ ] Read
[ ] Write
```

Default:

```text
Execute checked
```

### Address Section

Controls:

```text
Start Address
[ ] Range
End Address
```

When Range is disabled:

```text
End Address disabled
```

### Mapping Section

Controls:

```text
Map
ROM
RAM
```

Mutually exclusive.

Default:

```text
Map
```

### Counter Section

Controls:

```text
[ ] Use Counter
Initial Count
Reset Count
```

Count fields disabled unless enabled.

### Actions Section

Controls:

```text
[ ] Break
[ ] Fast
[ ] Slow
[ ] Tron
[ ] Troff
[ ] Type
[ ] Swap
```

Future actions may require additional parameter fields.

Phase 13 may disable parameter editing for unimplemented actions.

### Buttons

```text
Cancel
Apply
```

Cancel:

```text
Discard changes
```

Apply:

```text
Validate
Send runtime command
Close dialog
```

---

## Breakpoint List Integration

The existing breakpoint view should evolve into:

```text
New
Edit
Duplicate
Enable/Disable
Clear
Clear All
```

Disabled breakpoints remain visible.

Disabled breakpoints are persisted.

---

## INI Persistence

Use a normal INI section.

```ini
[DEBUG]
```

Do not reuse repeated-key syntax.

Breakpoint entries use:

```text
break.<address>
```

If duplicate start or range keys exist, save deterministic suffixes:

```text
break.<address>
break.<address>.1
break.<address>.2
```

Suffixes avoid INI key collisions only.

They are not runtime identity.

Examples:

```ini
[DEBUG]

break.C000=execute,map,break

break.C000.1=write,ram,break

break.C000.2=read,rom,tron

break.C000-C0FF=read,write,map,break

break.D000-DFFF=read,write,ram,break

break.C100=execute,map,break,count=10,reset=2

break.FCE2=execute,map,break,tron
```

On save, runtime emits deterministic keys and adds `.1`, `.2`, etc. when duplicate start or range keys exist.

On load, runtime accepts suffixed keys and assigns fresh runtime IDs.

Accepted keywords:

```text
execute
read
write
map
rom
ram
break
fast
slow
tron
troff
type
swap
count=<n>
reset=<n>
enabled
disabled
```

Parser behavior:

```text
unknown keywords ignored with warning
invalid breakpoint ignored
remaining valid breakpoints continue loading
```

---

## Suggested Implementation Order

1. Runtime breakpoint model redesign
2. Runtime command and snapshot API through runtime_client
3. Runtime breakpoint evaluation redesign
4. Runtime counter support
5. Runtime action framework
6. INI serialization and loading
7. Regression tests for runtime behavior and INI
8. Modal dialog framework
9. Breakpoint editor dialog
10. Breakpoint list integration
11. Frontend/UI smoke testing
12. STATUS.md update

---

## Non-Goals

Do not implement:

```text
conditional expressions
symbol-based breakpoints
disk swap functionality
trace implementation details
text injection implementation details
source-level debugging
frontend ownership of breakpoint state
machine ownership of debugger state
```

---

## Acceptance Criteria

Phase 13 is complete when:

```text
breakpoints support execute/read/write triggers
ranges work
mapping filters work
runtime owns all breakpoint evaluation
multiple actions may be attached to one breakpoint
counters work
disabled breakpoints persist
breakpoints save and load through INI files
breakpoint editor dialog works
existing execute breakpoint behavior still works
frontend never reads live machine state
all existing tests pass
new breakpoint tests pass
STATUS.md accurately reflects completion
```
