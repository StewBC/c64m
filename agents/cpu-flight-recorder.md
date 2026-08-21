# CPU flight recorder feature specification

**Status:** implemented (2026-07-25).

This document is the normative product and architecture specification for the
feature that replaced the former 64-entry CPU-history implementation. The sequence
and test-first work breakdown are in `cpu-flight-recorder-plan.md`.

The source and tests are authoritative for current behavior.

## Decision summary

- Build a bounded, in-memory forensic flight recorder for the main C64 6510.
- Record by default with a configurable 256 MiB byte budget.
- Keep recording cheap enough to leave enabled: target <=5% turbo-2 throughput
  loss, hard ceiling 10%.
- Search only while the machine is explicitly paused.
- Keep reset history and mark the new reset timeline.
- Clear history after a successful state load; never serialize history into a
  machine snapshot.
- Serve bounded binary pages through token-keyed RPC. Never put the history or
  result pages by value in `runtime_event`.
- Replace the old history commands and bump the wire protocol. There is no
  backward-compatibility requirement inside or outside this repository.
- Deliver the recorder core and remote API first. A UI history browser is a
  follow-up. Time travel is a separate checkpoint-and-replay feature.

## Why this exists

Breakpoints and TRON/TROFF require the investigator to know where or when to
start looking. They are good forward-debugging tools, but they cannot answer the
retrospective question:

> The machine is wrong now. What executed or accessed memory before it became
> wrong?

The recorder is a black box for that question. Its primary use cases are:

1. Find the most recent instruction that read, wrote, or executed an address or
   address range, then retrieve surrounding execution context.
2. Recover the control-flow path into a crash, BRK, unexpected IRQ/NMI handler,
   bad vector, corrupted register, or stuck loop.
3. Find an executed opcode sequence, including wildcarded patterns, and inspect
   the state and bus interactions around each occurrence.

The recorder complements rather than replaces:

- breakpoints, for stopping on a known future condition;
- `write_history[65536]`, which cheaply feeds the existing debugger display;
- TRON/TROFF, for explicitly requested unbounded disk logs;
- save states, which capture machine state rather than execution history.

## Goals

- Millions of retained execution records, not hundreds.
- Always-on surprise value: history normally exists before the user knows it is
  needed.
- Bounded resident memory selected by configuration.
- No allocation, locking, disassembly, string formatting, file I/O, event
  publication, or machine snapshot copy on the recording hot path.
- Preserve the current batched `c64_step_cycles_ex()` free-run path.
- Preserve actual fetched instruction bytes so self-modifying and banked code is
  not disassembled from later memory contents.
- Record the real CPU bus accesses that occurred, including accesses with
  hardware-visible side effects.
- Stable ordering across machine resets, even though `c64_reset()` zeros the
  machine master clock.
- Bounded, deterministic, token-correlated remote responses.
- A shared internal query API that a future UI can consume without depending on
  the control protocol.

## Non-goals

- Drive 8 or drive 9 CPU recording in the first version.
- VIC-II DMA/fetch history.
- A permanent or unbounded trace.
- General-purpose query expressions, SQL, regular expressions, or arbitrary
  aggregation in the first version.
- Concurrent searching while execution continues.
- Reverse execution or restoration of old machine states.
- Persisting recorded history contents in `.c64state`, INI files, crash dumps,
  or any other file. The memory-budget setting itself is normal configuration.
- Replacing TRON/TROFF.
- A frontend history window in the first delivery.

## Terminology and identity

### History epoch

`epoch` identifies one lifetime of retained history. It changes when history is
explicitly cleared or a state snapshot is successfully loaded. Record IDs may
restart at 1 in a new epoch.

The stable external identity of a record is:

```text
(epoch, id)
```

No API may accept a bare ID without also binding it to the current epoch, either
explicitly or through a cursor.

### Timeline

`timeline` distinguishes machine-clock domains within an epoch. It increments
when reset causes the machine master clock to restart, or when a retained
configuration change alters the machine clock rate without a reset.

Reset retains older records, starts a new timeline, and inserts a reset marker.
A successful state load instead clears the recorder and starts a new epoch.

### Record

A record is one of:

- `instruction`: one executed 6510 instruction;
- `irq`: one hardware IRQ entry sequence;
- `nmi`: one hardware NMI entry sequence;
- `marker`: a runtime-originated discontinuity or debugging event.

`id` orders all record kinds. IRQ/NMI entries are records because they consume
bus cycles and change CPU state without fetching a normal opcode.

### Machine cycle

Each record carries the machine master cycle at which it began. This value is
meaningful within its `timeline`; it is not globally monotonic across reset.
Record ID is the authoritative ordering key.

## Logical data model

The physical ring encoding is private to the runtime and may change without a
wire-protocol change. It provides the following logical fields, with any
exception made explicit by the access/timing truncation flags.

### Instruction record

- `epoch`
- `id`
- `timeline`
- machine start cycle
- opcode PC
- pre-instruction A, X, Y, SP, and P
- actual opcode byte fetched
- zero, one, or two actual operand bytes fetched
- instruction length, 1..3
- zero or more retained CPU bus accesses in occurrence order
- flags:
  - complete or partial;
  - access list truncated;
  - access timing truncated;
  - marker/machine discontinuity where applicable

The CPU registers are the values before the instruction begins. The opcode and
operand bytes are the values observed by the CPU, not bytes reread from the
current memory map after execution.

### Interrupt record

IRQ and NMI records contain the same identity, cycle, PC, and pre-entry register
state as an instruction record. They have instruction length zero and no opcode
pattern value. Their retained accesses include dummy, stack, and vector bus
cycles.

An interrupt record ends when the CPU interrupt micro-sequence completes, before
the first handler opcode begins.

### Bus-access record

Each retained access contains:

- logical 16-bit CPU address;
- value read or written;
- semantic access kind;
- cycle offset from its containing execution record.

Cycle offsets are 16-bit because all supported C64 CPU/BA sequences are far
shorter than 65,535 cycles. If a future machine path violates that invariant,
the recorder saturates the affected offset to 65,535 and sets
`timing-truncated`; it does not corrupt or resize the arena record.

The required access kinds are the existing `c6510_bus_access_kind` categories:

- opcode fetch;
- operand read;
- data read;
- data write;
- dummy read;
- RMW dummy write;
- stack read;
- stack write;
- vector read.

Opcode and operand fetch values are stored in the instruction header and need
not be duplicated in the internal variable access list. The query layer must
nevertheless expose them as searchable access categories.

All other categories are retained, including dummy and RMW accesses. They can
have I/O side effects and must not be collapsed into a single “effective
address.”

The private format retains at most 64 non-fetch accesses per execution record
(`RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD`). This is above every legitimate
6510 instruction/interrupt sequence. Exceeding it preserves the first 64,
sets `access-truncated`, and never overwrites the following record.

### Partial record

Pausing can occur after a watchpoint hits in the middle of an instruction. The
active record is therefore queryable with `partial=1`; it contains every access
observed before the pause. It becomes complete only when the machine reports
instruction/interrupt completion.

A pause/query/resume with no intervening mutation continues the same active
record. A reset, recording stop, direct host mutation, or other discontinuity
that occurs mid-instruction instead seals the record with `partial=1`, ignores
the remainder of that CPU instruction, and resumes observation at the next
instruction/interrupt begin. The following marker explains the gap.

Starting a new execution record while a previous record remains active is a
recorder invariant violation. A sealed partial record is no longer active and
does not violate this rule.

### Markers

Markers are rare fixed-size records. Required first-version marker kinds are:

- recorder start;
- recorder stop/gap;
- reset complete;
- successful PRG/BIN injection;
- successful CRT attach;
- successful assemble/direct memory mutation;
- KERNAL LOAD/SAVE trap;
- successful state load (the first marker in the new epoch);
- machine configuration discontinuity that resets the machine clock.

Markers use small numeric arguments, not path strings. They exist to explain a
state discontinuity, not to reproduce host operations. Adding a marker must not
perform heap allocation on the execution hot path.

HST1 fixes these marker IDs and arguments:

```text
1  recorder-start       no arguments
2  recorder-stop        no arguments
3  recorder-resume      no arguments
4  reset-complete       arg0=stable HST1 reset kind
5  state-load           no arguments
6  program-inject       arg0=start address, arg1=byte count
7  crt-attach           arg0=cartridge type
8  assemble             arg0=start address, arg1=byte count
9  direct-memory-write  arg0=start address, arg1=byte count
10 kernal-load-trap     arg0=start address, arg1=byte count
11 kernal-save-trap     arg0=start address, arg1=byte count
12 clock-discontinuity  arg0=stable HST1 clock-discontinuity kind
```

Unknown future marker IDs must be skipped/presented as unknown using the
record’s encoded size, not treated as malformed.

HST1 also fixes the following reset-kind values for marker 4 `arg0`:

```text
0 unknown/future call site
1 initial-startup
2 explicit-reset
3 machine-config
4 crt-attach
5 program-load
6 assemble-reset-first
7 binary-load-reset-first
```

`program-load` includes the current PRG/T64 boot-and-inject path. These values
describe externally meaningful causes, not internal function or command enum
values. A future reset cause gets a new value; it must not reuse or renumber an
existing value.

HST1 fixes the following clock-discontinuity values for marker 12 `arg0`:

```text
0 unknown/future clock-domain change
1 video-standard-change
```

Marker 12 is emitted only when retained history spans a clock-rate change
without a full reset. It starts a new timeline at the current machine cycle. If
the same configuration operation performs a full reset, emit only
`reset-complete` with reset kind `machine-config`; do not create a redundant
clock-discontinuity marker.

Keyboard, joystick, paste, disk mount/swap, and other host-input markers are
optional in the first implementation. They become required only as part of a
future deterministic replay design.

## Recorder ownership and machine observation

The runtime thread owns the recorder and is its only writer. No live `c64_t`
pointer crosses a thread boundary.

The recorder itself belongs in a new runtime module, expected to be:

```text
src/runtime/runtime_history.c
src/runtime/runtime_history.h
```

Machine code provides observation hooks but does not own the ring. The
observation contract needs callbacks for:

- normal instruction begin;
- IRQ/NMI begin;
- actual CPU bus access;
- instruction/interrupt completion;
- successful machine-side KERNAL LOAD/SAVE trap.

The observer begin payload contains the pre-execution CPU state and machine
cycle. The access payload contains `c6510_bus_access_kind`, address, value, and
actual machine cycle. The trap notification is a small fixed payload containing
a machine-defined kind plus start address and byte count; the runtime maps it to
a history marker.

A successful host trap can be reported while an execution record is active.
The adapter stores at most one pending trap marker in fixed runtime state and
appends it immediately after that record completes. If a discontinuity seals
the record partial first, flush the pending trap marker after the partial record
and before changing timeline or inserting the discontinuity marker. No marker
is inserted inside an execution record.

The existing memory-access breakpoint callback should be folded into or adapted
to this richer observer rather than adding a second per-access runtime callback.
When recording is unavailable/off and no read/write watchpoint needs access
events, the machine observer may be null.

### Normal microcycle path

Open the instruction record before `c6510_micro_begin()`. The opcode-fetch
callback replaces/confirms the opcode with the actual fetched value. Operand
callbacks fill operand bytes; other retained callbacks append access records.
Complete the record when the CPU micro-instruction completes.

### Compatibility/deferred replay path

`c64_prepare_deferred_cpu_trace()` runs an instruction against a frozen device
world and later replays its bus events with BA/AEC timing. The recorder must:

1. capture pre-instruction state before speculative `c6510_step()`;
2. never record speculative accesses while `C64_CPU_BUS_MODE_DEFER_WRITES` is
   generating `pending_cpu_trace`;
3. record accesses only when `c64_apply_cpu_bus_event()` applies them to the live
   machine;
4. complete the record only when deferred playback finishes.

This avoids duplicate accesses and prevents post-instruction registers from
being mislabeled as pre-instruction state.

### Interrupts and BA stalls

Open IRQ/NMI records in the actual interrupt-begin path, including the
between-instruction BA-stall/deferred-interrupt case. Cycle offsets use actual
machine cycles so BA delay is represented.

### Runtime free-run path

Recording must not make `runtime_free_run_is_simple()` false. The observer is
called from inside the machine’s batched cycle stepping, so history remains
compatible with `runtime_step_cycles_free_run()` and `c64_step_cycles_ex()`.

TRON’s `cpu_trace_enabled` machinery remains independent. The recorder must not
enable the existing 64-event instruction trace merely to collect its own data.

## Storage design

### Allocation

- Default requested budget: 256 MiB.
- Configuration accepts `0` (unavailable by configuration) or 16..4096 MiB.
- Allocation occurs during runtime creation before execution begins.
- Allocation failure is nonfatal. Emulation continues without history.
- Do not silently substitute a smaller buffer. `history-info` reports the
  requested and actual capacity and the unavailable reason.
- No UI alert is required. One diagnostic log line is acceptable.
- Partition metadata and arena blocks from the requested budget without
  zero-filling the record-data region; initialize descriptors only. Encoders
  may read only each block’s explicitly used bytes.

Metadata and alignment may reduce usable record bytes below the requested byte
count. `history-info` reports both requested and usable capacity.

### Segmented circular arena

Use a preallocated byte arena divided into fixed blocks; 64 KiB is the initial
block size. Each block descriptor records at least:

- epoch;
- timeline;
- first and last retained ID;
- base machine cycle;
- used bytes;
- record count;
- active/sealed state.

Records never span blocks. Before opening a record, ensure room for the maximum
legal record; otherwise seal the active block and advance to the next block.
Wrap evicts whole oldest blocks. This keeps append and eviction constant-time
and makes scans able to reject blocks by ID/timeline/cycle metadata.

Changing timeline seals the current block and begins a new block, allowing
compact cycle deltas even though reset sets the machine cycle back to zero.
If the next record’s cycle delta would exceed 32 bits, seal the block and use
that record’s start cycle as the next block base.

### Initial private encoding

The first implementation should start with a simple encoding rather than an
elaborate compressor:

```text
22-byte execution header
  uint32 cycle_delta_from_block_base
  uint16 pc
  uint8  a, x, y, sp, p
  uint8  opcode, operand1, operand2
  uint16 opcode_cycle_offset, operand1_cycle_offset, operand2_cycle_offset
  uint8  access_count
  uint8  tag/length/partial/truncated flags

6 bytes per retained non-fetch access
  uint16 address
  uint16 cycle_offset
  uint8  value
  uint8  access_kind
```

Marker records reuse a fixed 22-byte header with a marker-specific
interpretation.

All arena encoding/decoding is byte-oriented and endian-independent inside its
helpers. Do not cast unaligned arena bytes to C structs.

The implementation may change this encoding after Phase 0 measurements, but it
must document the measured reason. Delta-register compression is not a first
step: it adds branches to the hot path and complicates random decoding before
the simple design has been measured.

### Optional search indexes

The first correct query implementation may scan block records linearly.
Per-block PC/address Bloom filters or other derived indexes are permitted only
if measured query latency needs them.

Do not add hashing/index maintenance to every access before measuring a linear
scan. If indexes are added, prefer building them from a sealed block or otherwise
prove their recording cost stays within the performance gate.

## Lifecycle

### Startup

If allocation succeeds and the configured budget is nonzero, recording starts
before the initial machine reset/boot and inserts a recorder-start marker in
timeline 0. The first successful reset starts timeline 1; subsequent successful
resets increment it.

If allocation fails, state is:

```text
available=0 recording=0 reason=allocation-failed
```

If configured budget is zero:

```text
available=0 recording=0 reason=disabled-by-config
```

### Recording control

- `history-record off` seals any active partial record, inserts a recorder-stop
  marker, then stops appending while preserving retained data.
- `history-record on` resumes in the same epoch and inserts a gap/resume marker.
- Repeating the current state is idempotent.
- `history-clear` removes all retained records, increments epoch, resets record
  IDs, invalidates query cursors, and preserves the recording on/off state.
- If recording is on after clear, insert a recorder-start marker in the new
  epoch.

If recording is stopped or resumed while the CPU is mid-instruction, apply the
partial-record policy above. Resuming arms observation at the next execution
begin; it does not invent pre-instruction state for an instruction already in
progress. Clearing while recording is on follows the same rule after inserting
the new epoch’s recorder-start marker.

### Reset

Successful reset:

- preserves old history;
- increments timeline;
- seals the old active block;
- inserts a reset-complete marker at the new machine cycle;
- continues with the next record ID.

Failed reset does not create a new timeline or marker.

### Save state

Saving has no recorder effect. No history bytes, metadata, capacity setting,
cursor, epoch, or recording state enter the machine snapshot.

### Load state

Failed state load preserves history.

After `c64_snapshot_load()` succeeds:

- clear retained history;
- increment epoch;
- start timeline 1 using the restored machine clock;
- invalidate cursors;
- insert a successful-state-load marker if recording is on.

History clearing belongs in the runtime’s post-load transient handling, not in
the machine snapshot serializer.

### Other mutations

Program injection, cartridge attach, assembly, and direct host memory writes
retain history and add a marker after successful mutation. Operations that reset
the machine also follow the reset/timeline rule.

Marker ordering follows actual successful commit order, not a preferred
presentation order:

- CRT attachment currently commits the cartridge and then resets. Append the
  CRT marker in the old timeline immediately after attachment succeeds, then
  append `reset-complete(crt-attach)` as the first marker in the new timeline.
- PRG/T64 load, reset-first BIN load, and reset-first assembly reset before
  their later injection/assembly. Their reset marker therefore precedes the
  mutation marker, which appears later in the new timeline when that mutation
  actually succeeds.
- If the first operation succeeds and the following operation fails, retain the
  first operation’s marker. Never suppress or reorder it to make the combined
  command appear atomic.

Cursor invalidation stays off the recording hot path. Before resume, step,
reset, load, direct mutation, recording control, or any other operation that
can append or alter recorder contents, the runtime marks the active cursor stale
and increments a 64-bit mutation generation once. The begin/access append
functions do not update that generation per record or access. Query cursors
capture the generation and become stale when it changes.

Because searches are paused-only, recorder appends and a valid cursor cannot
normally coexist. Debug builds assert this contract at execution entry; a
violation invalidates the cursor before writing rather than exposing changing
arena data.

## Query behavior

### Pause requirement

`history-find`, `history-next`, and `history-read` require
`RUNTIME_EXEC_PAUSED`. They never auto-pause. When running they return:

```text
<request-id> error busy machine-running
```

`history-info`, `history-record`, `history-clear`, and `history-close` are
runtime commands and may be used while running.

The runtime performs searches against its frozen arena. It returns bounded
owned payload pages; it never exposes arena pointers through `runtime_event`,
the main thread, or the socket thread.

### Search filters

The first version supports:

- exact PC or inclusive PC range;
- exact logical CPU address or inclusive address range;
- access categories;
- optional accessed value and mask;
- exact epoch/current epoch;
- timeline;
- machine-cycle range;
- start record ID;
- direction, backward or forward;
- executed-opcode sequence with wildcard/masked bytes.

All supplied predicates are conjunctive and apply to the candidate anchor
record. An opcode pattern begins at the anchor. Markers, IRQ/NMI records, epoch
boundaries, and timeline boundaries break opcode sequences.

An empty filter can return every record kind, including markers. A `pc` filter
matches instruction and IRQ/NMI pre-entry PCs, never marker placeholder fields.
`execute` and opcode-pattern predicates match normal instruction records only.
Physical access predicates can match instruction or IRQ/NMI records.

Backward/newest-first is the default.

Opcode pattern syntax is a comma-separated list of 1..32 byte masks:

```text
opcodes=A9,??,8D
opcodes=A?,8D
```

`??` matches any opcode and a single `?` nibble masks that nibble. These are
sequences of executed opcodes, not raw adjacent memory bytes.

Canonical access names are:

```text
execute opcode operand data-read data-write dummy-read rmw-dummy-write
stack-read stack-write vector-read
```

Convenience aliases may expand as:

```text
fetch = opcode,operand
read  = opcode,operand,data-read,dummy-read,stack-read,vector-read
write = data-write,rmw-dummy-write,stack-write
data  = data-read,data-write
```

`execute` is a predicate on the execution record’s PC. It is not an additional
physical bus-access entry; the opcode-fetch entry represents the actual bus
cycle at the same address.

The `access` option accepts a comma-separated union of canonical names and
aliases. Alias expansion produces a bit mask; repeated categories are harmless.
An empty list or unknown name is `bad-args`.

With `address` and no `access`, any materialized physical access kind can match,
including opcode and operand fetch. With `access=execute`, `address` is matched
against the execution PC instead. A `value` predicate applies to the same
physical access that satisfied the address/access predicate; for `execute`, it
applies to the executed opcode. If neither `address` nor `access` is supplied,
`value` searches all physical accesses.

### Cursor

History search cursors are **per session** (fixed table of 4 asker slots on the
runtime; see `agents/sessions.md`). A new `history-find` on a given session
replaces that session’s cursor only. Omitting session id uses the default
compat session. The cursor owns only query criteria and a scan position, never
arena data.

The cursor binds:

- epoch;
- recorder mutation generation;
- full query;
- direction;
- next scan position.

`history-next` returns `stale history-cursor-stale` after cursor invalidation
(mutation invalidates **all** active session cursors). `history-close` is
idempotent. Disconnect releases the control session slot; a later
`history-find` on a new session starts a fresh cursor.

Cursor IDs are nonzero unsigned 64-bit values and are not reusable within one
runtime lifetime. A successful find page reports a cursor only when more search
space remains; otherwise it reports `cursor=0 more=0` and closes the cursor.
`history-next` follows the same rule. A search with no matches is a successful
zero-record page, not `not-found`. `history-read` always reports `cursor=0`;
its `more` bit means the requested context was clipped by the record or payload
limit.

### Reading context

Search and context retrieval are separate:

- `history-find` / `history-next` return matching detailed records.
- `history-read` returns a contiguous window around one `(epoch,id)`.

This lets a client find “last write to `$D015`” and then request, for example,
32 records before and 8 after without making every search result duplicate its
context.

Maximums:

- find page: 1..256 matches, default 64;
- read context: `before` and `after` each 0..256;
- encoded result payload: 1 MiB hard cap.

If the record limit or payload cap is reached, metadata reports `more=1`.

## Remote protocol

### Protocol version and replacement

The implementation bumps C64M/2 to **C64M/3**.

Remove:

```text
set-cpu-history
get-cpu-history
```

Add:

```text
history-info
history-record <on|off>
history-clear
history-find [options...]
history-next <cursor> [limit=<1..256>]
history-read <id> [epoch=<n>] [before=<0..256>] [after=<0..256>]
history-close <cursor>
```

`capabilities` advertises `history` when the build includes the feature even if
runtime allocation failed. Runtime availability comes from `history-info`.

### `history-info`

Example available response:

```text
<request-id> ok available=1 recording=1 requested_bytes=268435456
   capacity_bytes=268369920 used_bytes=73400320 epoch=3 timeline=2
   records=3145728 oldest=24 newest=3145751 wrapped=0 partial=0
   truncated_accesses=0
```

Example unavailable response:

```text
<request-id> ok available=0 recording=0 requested_bytes=268435456
   capacity_bytes=0 reason=allocation-failed
```

The actual response is one protocol line; wrapping above is illustrative.
Raise `CONTROL_RESPONSE_TEXT_MAX` to 512 so worst-case 64-bit status values do
not truncate this line. History record pages still use binary payloads.

History clients should issue `history-info` at the beginning of a debugging
session.

### `history-find`

Options use whitespace-separated `key=value` tokens:

```text
history-find address=$D015 access=write direction=backward limit=64
history-find pc=$E000-$EFFF opcodes=A9,??,8D timeline=2 limit=32
history-find cycle=100000-200000 direction=forward
```

Supported keys:

```text
epoch timeline cycle from direction pc address access value opcodes limit
```

- `from` is a record ID, `oldest`, or `newest`.
- `direction` is `backward` (default) or `forward`.
- If `from` is omitted, backward starts at newest and forward starts at oldest.
  The starting record is inclusive. An explicit ID that is not retained returns
  `not-found history-record-not-retained`.
- address and PC values use the existing 16-bit address syntax.
- ranges are non-wrapping and inclusive; an end below its start is `bad-args`.
- ID, epoch, timeline, and cycle values are unsigned decimal/base-0 integers.
- `value=VV` means mask `$FF`; `value=VV/MM` supplies an explicit mask.
  `VV` and `MM` are exactly two hexadecimal digits.
- `value` applies to the access that satisfies `address`; for
  `access=execute`, the matched value is the executed opcode.
- duplicate or unknown keys are `bad-args`.
- an empty filter is allowed and pages records in the selected direction.

### `history-read`

`id` is the anchor. The default epoch is the current epoch. Results are returned
in chronological order regardless of search direction. Defaults are
`before=32 after=8`; specify zero explicitly to retrieve only the anchor. A
missing/evicted ID is:

```text
<request-id> error not-found history-record-not-retained
```

### Binary result payload

Find, next, and read return:

```text
<request-id> data history <byte_count>
    epoch=<n> count=<n> cursor=<n> more=<0|1> oldest=<id> newest=<id>
<binary payload>
```

The header line is followed by exactly `byte_count` payload bytes and then one
newline delimiter, matching the existing counted binary-data framing. In the
metadata, `oldest` and `newest` are the smallest and largest record IDs present
in this result page, not the arena retention bounds.

All integers are explicitly encoded little-endian. Never `memcpy()` a native C
struct onto the wire.

Payload header, version 1, 24 bytes:

```text
offset size field
0      4    ASCII "HST1"
4      2    format version = 1
6      2    flags/reserved = 0
8      8    epoch
16     4    record count
20     4    reserved = 0
```

Each record begins with a 48-byte header:

```text
offset size field
0      2    encoded record size, including access entries
2      1    record kind: instruction=0, irq=1, nmi=2, marker=3
3      1    flags: bit0 partial, bit1 access-truncated, bit2 anchor-match,
            bit3 timing-truncated
4      4    timeline
8      8    record ID
16     8    machine start cycle
24     2    PC
26     1    A
27     1    X
28     1    Y
29     1    SP
30     1    P
31     1    opcode
32     1    operand byte 1
33     1    operand byte 2
34     1    instruction length, 0..3
35     1    access-entry count
36     2    marker kind, zero for non-marker
38     2    reserved
40     4    marker argument 0
44     4    marker argument 1
```

Each following access entry is 8 bytes:

```text
offset size field
0      2    logical CPU address
2      2    cycle offset from record start
4      1    value
5      1    c6510 bus-access kind
6      2    flags/reserved
```

Wire access-kind values are fixed for HST1 and match the current machine enum:

```text
0 data-read       1 data-write       2 opcode
3 operand         4 dummy-read       5 rmw-dummy-write
6 stack-read      7 stack-write      8 vector-read
```

Instruction opcode/operand fetches are always materialized as access entries by
the wire encoder, even though the private arena stores their values and cycle
offsets in the execution header.

The encoder sets `anchor-match` on every find/next result and on the
`history-read` anchor. Context records surrounding a read anchor do not set it.
For an empty page, `oldest=0 newest=0`.

### Delivery and concurrency

- Every history runtime request is token-bearing.
- Completion events contain metadata/status only.
- Binary payloads use a token-keyed owned result pool.
- Refactor the memory-specific RPC pool into a generic bulk payload pool or add
  an equivalent history pool; do not enlarge `runtime_event`.
- Claiming a payload moves ownership to the response. Queue failure, timeout,
  disconnect, shutdown, and cancellation free it.
- History commands are exclusive deferred operations in the first version. A
  second outstanding history operation returns `busy`.
- `history-find` timeout is 10 seconds; other history RPCs use 2 seconds.
- Result order follows the command contract, not event queue arrival order.

## Error contract

Required error codes/messages:

```text
unavailable history-recorder-unavailable
busy        machine-running
busy        history-request-active
bad-args    <specific parse/validation reason>
stale       history-cursor-stale
stale       history-epoch-mismatch
not-found   history-record-not-retained
runtime     history-query-failed
```

Allocation failure is not an application startup failure. It is observable
through `history-info` and history-command errors. `history-info` and the
idempotent `history-close` remain available; record, clear, find, next, and read
return `unavailable` when no arena exists. This is distinct from a successful
find/next result with zero matches.

## Configuration

Add:

```ini
[debug]
history_memory_mb=256
```

Add:

```text
--history-memory=<MiB>
```

Accepted values are `0` or 16..4096. Command-line value overrides INI. Invalid
CLI values fail option parsing with a specific diagnostic. Invalid INI values
fall back to the 256 MiB default and log one diagnostic.

The budget is startup-only in the first version. The remote API can stop/start
recording and clear history, but cannot resize the arena.

The setting belongs in `app_options`, flows through `runtime_config`, and is
saved with the other persistent configuration. No frontend configuration widget
is required in the first version.

## Performance and capacity acceptance

### Measured legacy cost

On 2026-07-25, the current build was measured serially with:

```text
./build/c64m --headless --control-port=<port> -n -! -P --turbo=2
```

No drive was powered and video remained enabled:

```text
legacy history off: 14.339, 14.374 MHz
legacy history on:  10.595, 10.603 MHz
```

The approximately 26% loss is an upper bound from the obsolete design. It
disables simple batched free-run and performs a snapshot plus mapped-memory
lookup per instruction. The new recorder must not inherit that path.

### Delivered measurements

The implemented recorder measured 14.716/14.705 MHz with recorder capacity
disabled by configuration and 13.593/13.634 MHz with full access recording,
about a 7.4% throughput loss. This is above the 5% goal but below the 10% hard
ceiling and was explicitly accepted without further optimization.

A Phase 3 follow-up pass recovered about 0.85% enabled throughput in direct
old/new binary comparisons. Final matched averages were 14.705 MHz with
recorder capacity disabled and 13.664 MHz with full recording, about a 7.1%
loss. The encoding and retention contract did not change.

The 256 MiB arena retains about 9.51 million records in the measured workload.
A full-store exact-address miss took 219.734 ms; a newest hit took 0.020 ms, a
PC query 1.4 ms, and a three-opcode pattern 0.058 ms. The linear query engine is
therefore retained for the first implementation.

### Gates

Measure enabled and disabled in the same serial benchmark session:

- goal: <=5% turbo-2 throughput loss;
- temporary optimization budget: >5% and <=10%;
- hard stop: >10% after the simple recorder is implemented.

If the hard stop is exceeded, do not proceed to the full query/UI surface until
the hot path is redesigned or recording scope is deliberately reduced and this
spec is amended.

Disabled with no read/write watchpoints should be within 1% of the pre-feature
baseline.

At a 256 MiB budget, the idle BASIC/reference workload must retain at least
8 million execution records. Report actual average bytes per instruction and
accesses per instruction for idle BASIC and at least one access-heavy title.

Linear full-buffer exact-address search goals on the reference Apple M2:

- target <=100 ms;
- hard interactive ceiling 500 ms.

Search is paused-only, so it does not reduce emulation throughput. If the scan
misses the ceiling, add measured per-block indexes without violating the
recording gate.

## Required correctness coverage

At minimum, automated tests must prove:

- record ordering and pre-instruction register state;
- actual opcode/operand bytes under self-modifying code;
- data read/write values and addresses;
- dummy, RMW dummy-write, stack, and vector classification;
- IRQ/NMI pseudo-records;
- BA-stall cycle offsets;
- no speculative/duplicate access records on deferred compatibility replay;
- partial record visibility on mid-instruction pause;
- whole-block wrap and oldest/newest ID maintenance;
- reset retains records, increments timeline, and handles cycle zero;
- save-state leaves history untouched;
- successful load-state clears to a new epoch;
- failed load-state preserves the prior epoch;
- off freezes, on inserts a gap marker, clear starts a new epoch;
- allocation failure is nonfatal and remotely observable;
- searches for PC, address/access/value, ranges, cycles, direction, and wildcard
  opcode sequences;
- cursor invalidation before resume/step or any lifecycle/direct mutation;
- binary payload byte order, sizes, truncation limits, and malformed-client
  rejection;
- token matching, queue-full cleanup, timeout cleanup, disconnect cleanup, and
  no by-value event growth;
- removal of old commands and correct C64M/3 identity.

The detailed test mapping and implementation order are in
`cpu-flight-recorder-plan.md`.

## Future UI

A later frontend history browser should use the same runtime query structures,
not parse control-protocol payloads. Candidate interactions:

- click a record to navigate disassembly;
- click an access to navigate memory/I/O;
- show preceding/following execution;
- “last writer/reader/executor” from memory views;
- promote a historical result to a live breakpoint;
- show IRQ/NMI/reset markers on an execution timeline.

No UI code is part of the initial implementation.

## Future time travel

The recorder is not sufficient to undo machine execution. VIC-II, CIA, SID,
interrupt pipelines, powered 1541s, media rotation, read side effects, and host
inputs evolve independently of CPU register/RAM changes.

Future time travel should use periodic lightweight in-memory checkpoints plus a
deterministic host-input/mutation log, then restore and replay to a recorder
record ID. This recorder helps with searchable destinations and validation, but
checkpoint/replay has its own feature brief, performance budget, and correctness
work.
