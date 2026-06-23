# C64MDASM Disassembly View Proposal

## Purpose

This proposal describes how the C64MDASM disassembly view should behave and how the rendering algorithm should be changed to support that behavior reliably.

The current problem is not simply "find a good top address." The real problem is:

> The program counter is the visual execution point. The disassembly view should be built around it.

When the emulator is running or stepping, the PC line should behave like a mostly static execution cursor, with the code scrolling through that cursor. The highlighted PC row, currently shown as the yellow line, should be treated as the thing that executes the code.

This means the PC is not just another address that may or may not appear in the decoded output. It is a hard synchronization point for the view.

## Golden Rules

### 1. The PC must always start a line

Whenever the PC address is visible, it must be rendered as the start address of a disassembly line.

This must hold under all circumstances.

The renderer must never allow an instruction decoded before the PC to consume the PC byte as an operand or as the second or third byte of an instruction.

In other words, this is forbidden in PC-aware rendering:

```text
previous_instruction: consumes bytes up to and across PC
PC: hidden inside previous instruction
```

This is required instead:

```text
previous bytes, decoded as well as possible
PC:
    decoded instruction at PC
```

If the bytes before the PC cannot be decoded into a clean instruction stream that ends exactly at the PC, the renderer should emit one or more `.byte` lines before the PC to force synchronization.

Example:

```asm
    .byte $xx
pc_label:
    lda #$01
```

or:

```asm
    .byte $xx
    .byte $yy
pc_label:
    lda #$01
```

The important invariant is:

> No decoded instruction before the PC may cross the PC.

### 2. During execution, the PC line number should stay fixed when at all possible

When the emulator is running, stepping, or otherwise advancing execution, the PC should be pegged to a stable row in the view.

Usually this should be the middle line:

```text
pc_row = rows / 2
```

The result should look like the code is scrolling through the yellow execution line, not like the yellow line is jumping around the view.

This is the desired visual model:

```text
        code before PC
        code before PC
        code before PC
yellow: PC instruction being executed
        code after PC
        code after PC
        code after PC
```

As execution advances, the disassembly content should move, while the yellow PC row remains stable whenever possible.

### 3. Manual scrolling is different from execution

When the emulator is stopped and the user manually scrolls the disassembly view, the PC does not have to remain pinned to the middle row.

In manual navigation mode:

- the user may scroll away from the PC;
- the PC line may move up or down if it is visible;
- the PC may leave the view entirely.

However, if the PC address is visible at all, it must still be rendered as a line start.

As soon as the user steps, runs, or otherwise resumes execution, the view should snap back to PC-locked mode and place the PC on the preferred execution row, normally the middle line.

## Current Algorithm Problem

The current design is approximately:

1. Pick a `top_address`.
2. Decode forward from `top_address`.
3. Hope that the PC appears near the middle of the view.

This is fragile for 6502 code because instruction boundaries are not self-evident. A byte can often be interpreted as an opcode, operand, or data depending on where decoding starts.

The fallback path is especially weak because it uses a byte offset:

```text
top_address = pc - rows / 2
```

That does not mean "put the PC halfway down the view." It means "start decoding some number of bytes before the PC." Since 6502 instructions vary from 1 to 3 bytes, this often starts in the middle of an instruction and may cause the decode stream to miss the PC as a line start.

The existing backward opcode search is better, but it is still too local. It asks:

```text
What could the previous instruction be?
```

But the better question is:

```text
What sequence of instruction boundaries best explains the bytes before PC and ends exactly at PC?
```

The difference matters because there may be several locally valid previous instructions. Only some of those choices may lead to a coherent stream when walking farther backward.

## Proposed Mental Model

The view should be built around anchors.

### Mandatory anchor

The PC address is a mandatory anchor.

When PC-locked mode is active, the PC anchor has two requirements:

```text
address == PC
row == preferred_pc_row
```

The preferred row should normally be:

```text
preferred_pc_row = rows / 2
```

### Optional anchors

Other addresses can be treated as hints:

- labels;
- symbols;
- branch targets;
- known previous line starts;
- user-selected addresses;
- debugger breakpoints;
- recent decode boundaries.

These hints may help determine likely instruction boundaries, but they must not override the PC invariant.

A label is probably a line-start address, but it cannot be trusted absolutely. It should improve a candidate's score, not force the result.

Example:

```asm
frame:
    sta $124
    ora $17
a:
    bcc frame
```

If the PC is at `a`, it is useful to test whether decoding from `frame` naturally reaches `a`.

If it does, that is a strong candidate for the rows before PC.

## Proposed Algorithm: PC-Anchored Best-Fit Disassembly

The recommended approach is a PC-anchored best-fit algorithm.

Instead of choosing a top address first, build the visible line list around the PC.

### High-level flow

```text
1. Decide whether PC-locked mode is active.
2. If PC-locked:
   a. place PC at preferred_pc_row;
   b. decode forward from PC to fill rows after PC;
   c. search backward for the best decode path ending exactly at PC;
   d. fill rows before PC from that path;
   e. use `.byte` fallback lines where needed to preserve PC alignment.
3. If manual-scroll mode:
   a. decode from the user's current top address;
   b. if PC appears in the visible range, force or repair the decode so PC is a line start.
```

## Best-Fit Search Before PC

The bytes before the PC should be treated as a small path-search problem.

Each address is a possible node. A decoded instruction creates an edge:

```text
address -> address + instruction_length
```

A `.byte` fallback creates another edge:

```text
address -> address + 1
```

The goal is to find the best path that ends exactly at PC.

### Search window

For 6502, instructions are 1 to 3 bytes. If we need `N` rows before the PC, then the useful backward range is roughly:

```text
PC - N * 3
```

Add some slop to allow for labels and imperfect alignment:

```text
search_start = PC - N * 3 - CENTER_SLOP
```

A reasonable initial value might be:

```text
CENTER_SLOP = 16 or 32
```

The fetch window should cover at least this region before PC.

### Candidate starts

Candidate start addresses should include:

1. known labels in the search range;
2. known branch targets in the search range;
3. known previous line starts in the search range;
4. every byte address in the search range as a fallback.

Labels and known line starts should be favored, but arbitrary byte addresses should still be considered so the algorithm remains robust.

### Dynamic programming shape

A simple dynamic programming pass can compute the best path from addresses before PC to PC.

Conceptually:

```c
score[pc] = 0;

for(address = pc - 1; address >= search_start; address--) {
    best_score = INF;

    len = decode_length(address);

    if(address + len <= pc && score[address + len] != INF) {
        consider_instruction_edge(address, address + len);
    }

    if(score[address + 1] != INF) {
        consider_byte_edge(address, address + 1);
    }
}
```

The `.byte` edge is always available when the byte exists in the snapshot. It should have a higher cost than a real instruction, so it is used only when necessary.

This is similar to a shortest-path or Viterbi-style decoding problem:

```text
bytes -> most plausible sequence of instruction boundaries
```

The search window is small, so this should be cheap enough to run whenever the PC changes, the snapshot changes, or the view size changes.

## Scoring

The exact numeric values can be tuned, but the scoring should express these priorities:

### Strong preferences

- path ends exactly at PC;
- path uses real instructions;
- path starts at a label or known line-start address;
- path passes through labels or branch targets at instruction boundaries;
- path fills the desired number of pre-PC rows.

### Penalties

- `.byte` fallback rows;
- starting at an arbitrary byte;
- using invalid or unsupported opcodes, if the decoder distinguishes them;
- producing too many or too few rows before PC.

### Forbidden

- any instruction before PC that crosses PC;
- any path that fails to make PC a line start;
- hiding PC as an operand byte;
- moving the PC row during PC-locked execution unless unavoidable due to missing memory or too few visible rows.

## Rendering Rows Before PC

After a best path is found, the renderer should select the suffix of that path that fills the rows before the PC.

If the best path has more rows than needed, use the last `needed_rows` lines before PC.

If the best path has fewer rows than needed, prepend more lines if possible. If not possible, use placeholders or `.byte` lines, depending on available memory.

The row immediately before PC must end exactly at PC.

Examples:

```asm
frame:
    sta $0124
    ora $17
a:
    bcc frame
```

If `PC == a`, and decoding from `frame` reaches `a`, then this should be preferred.

If no clean stream reaches `a`, but the last unknown bytes before `a` are available, the renderer should do something like:

```asm
    .byte $xx
    .byte $yy
a:
    bcc frame
```

rather than decoding an instruction that crosses into `a`.

## Rendering Rows After PC

Rows after PC are simpler.

Once the PC line has been emitted, decode forward from PC normally:

```text
address = PC
for(row = pc_row; row < rows; row++) {
    decode line at address
    address += line.length
}
```

Forward decoding from PC is authoritative because PC is a hard line-start anchor.

## Manual Scroll Mode

Manual scroll mode should preserve user navigation.

In this mode, the user controls the top address or scroll position. The renderer should not forcibly snap the PC to the middle.

However, the PC invariant still applies:

> If the PC address appears in the visible region, it must be a line start.

There are a few ways to handle this.

### Option A: PC-aware repair

Decode from the manual top address, but if the decode stream would cross the PC, repair the line immediately before PC with `.byte` output so the PC starts a line.

This keeps the user's scroll position mostly intact while protecting the PC invariant.

### Option B: segmented rendering

If the visible range includes PC:

1. render the portion before PC using best-fit search ending at PC;
2. render PC as a hard line-start;
3. render the portion after PC by decoding forward from PC.

This is more consistent with PC-locked mode and may produce better results, but it is more complex.

The recommended direction is Option B if the existing view architecture can support line-list rendering. Otherwise, Option A is an acceptable transitional improvement.

## Execution Mode and Snapping Behavior

The view should distinguish at least two modes:

```text
manual_scroll_mode
pc_locked_mode
```

### Entering manual scroll mode

Manual scroll mode begins when the user scrolls, drags, pages, jumps, or otherwise navigates the disassembly view manually.

In this mode, the view should not keep re-centering on PC.

### Entering PC-locked mode

PC-locked mode begins when the emulator runs, steps, breaks, or otherwise performs an execution action.

On entry to PC-locked mode:

```text
pc_row = rows / 2
```

The visible lines should be rebuilt around that PC row.

### During run or step

While execution is advancing:

- keep the yellow PC row fixed when possible;
- rebuild the visible line list as PC changes;
- make the code scroll through the fixed PC row;
- do not let transient decode uncertainty move the PC line.

This creates the intended execution illusion:

> The yellow line executes the code, and the code scrolls through it.

## Memory Fetching

The memory request should be driven by the PC-anchored view requirement.

For PC-locked mode, the fetch should include enough bytes before PC to solve the best-fit problem:

```text
request_start = PC - pre_pc_rows * 3 - CENTER_SLOP
```

It should also include enough bytes after PC to fill the rest of the view.

The current heuristic:

```text
PC - rows * 3 + 8
```

is suspicious because the `+ 8` reduces backward coverage. If the intent is to ensure enough bytes before PC, the direction should be more like:

```text
PC - rows * 3 - slop
```

or more explicitly:

```text
PC - pre_pc_rows * 3 - CENTER_SLOP
```

The fetch size can still be a fixed amount such as 1024 bytes, but the anchor should be based on the required pre-PC decode range.

## Snapshot Arrival Must Recenter

The current dependency problem is:

```text
Need snapshot to center correctly.
Need correct center to request a useful snapshot.
```

This should be broken by treating the first render as provisional.

If the PC changes and the snapshot is not yet available:

1. request memory around PC using the worst-case pre-PC range;
2. show a provisional view if necessary;
3. once the snapshot arrives, rebuild the PC-locked line list.

Snapshot arrival should be allowed to trigger recentering when PC-locked mode is active.

This avoids the failure mode where an initial bad byte-offset view remains wrong indefinitely.

## Data Structure Recommendation

The renderer should ideally produce an explicit list of visible lines, not only a `top_address`.

For example:

```c
struct frontend_disassembly_line {
    uint16_t address;
    uint8_t length;
    bool is_pc;
    bool is_label;
    bool is_byte_fallback;
    bool is_provisional;
    char text[...];
};
```

In PC-locked mode, this line list is built around the PC row.

This makes it easier to guarantee:

```text
lines[pc_row].address == PC
```

and:

```text
lines[pc_row].is_pc == true
```

A `top_address` can still be derived from `lines[0].address` for compatibility, but it should not be the primary authority in PC-locked rendering.

## Suggested Invariants

These should be asserted or at least logged in debug builds.

### PC-locked mode

```c
assert(lines[pc_row].address == pc);
assert(lines[pc_row].is_pc);
```

No pre-PC line may cross PC:

```c
for(i = 0; i < pc_row; i++) {
    assert(lines[i].address + lines[i].length <= pc);
}
```

The line immediately before PC should end at PC unless there is missing memory or a placeholder condition:

```c
assert(lines[pc_row - 1].address + lines[pc_row - 1].length == pc);
```

### Manual-scroll mode

If PC is visible:

```c
assert(pc appears only as a line start);
```

No decoded visible instruction should consume the PC address as a non-start byte.

## Minimal Implementation Plan

A practical incremental implementation could be:

### Step 1: Fix the fetch window

Change the PC-locked fetch anchor to cover the worst-case pre-PC range:

```text
request_start = PC - pre_pc_rows * 3 - CENTER_SLOP
```

This alone should make the snapshot more useful.

### Step 2: Add PC-anchored line building

Add a function such as:

```c
frontend_disassembly_build_pc_locked_lines(view, pc, rows);
```

This function should:

1. choose `pc_row = rows / 2`;
2. emit the PC line at `pc_row`;
3. decode forward from PC;
4. run best-fit search for rows before PC;
5. fill missing or ambiguous pre-PC bytes with `.byte` lines.

### Step 3: Recenter on snapshot update

When memory arrives, if PC-locked mode is active, rebuild the PC-locked line list.

Do not rely on the byte-offset fallback to become correct on its own.

### Step 4: Preserve manual scroll behavior

When the user scrolls manually, leave PC-locked mode.

If PC is visible during manual scrolling, ensure it is rendered as a line start. This may be implemented as a later improvement if necessary, but it is still part of the desired final behavior.

### Step 5: Add debug checks

Add assertions or logging for the PC-line invariants.

This will make regressions obvious.

## Proposed Function Names

Possible names:

```c
frontend_disassembly_build_pc_locked_lines()
frontend_disassembly_find_best_pre_pc_path()
frontend_disassembly_score_pre_pc_path()
frontend_disassembly_emit_byte_fallback()
frontend_disassembly_pc_visible_as_line_start()
```

## Summary

The disassembly view should not be thought of as a forward decode from a guessed `top_address`.

It should be thought of as a PC-anchored rendering problem.

The PC is the visual execution cursor. When executing, the yellow PC line should remain fixed when possible, and the code should scroll through it.

The core rules are:

1. PC must always be a line start when visible.
2. In execution mode, PC should stay on a stable preferred row, normally the middle row.
3. No instruction before PC may decode across PC.
4. If pre-PC bytes cannot be confidently decoded into PC, use `.byte` lines to force alignment.
5. Labels and branch targets are useful hints, but PC is the only mandatory anchor.
6. Snapshot arrival should rebuild the PC-locked view rather than leaving a bad provisional decode in place.

The best technical fit is a small dynamic-programming or shortest-path search over possible instruction boundaries before PC, followed by normal forward decoding from PC.
