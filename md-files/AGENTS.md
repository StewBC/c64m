# AGENTS.md

## Purpose

c64m is a C99 Commodore 64 emulator.

Goal:

```text
Reach a visible Commodore 64 BASIC startup screen.
```

## Required Reading Order

```text
1. AGENTS.md
2. STATUS.md
3. HIGHLEVEL.md
4. Current phase document
```

## Architecture

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
tools    -> util
platform -> util + SDL2
```

Never:

```text
frontend -> machine
platform -> machine
runtime  -> frontend
runtime  -> platform
machine  -> runtime
```

## Thread Ownership

```text
UI thread:
    SDL
    renderer
    frontend

Runtime thread:
    runtime
    live machine
```

The live machine exists only on the runtime thread.

No live machine pointers may cross threads.

## Snapshot Rule

Frontend receives copied snapshots only.

Frontend never reads live machine memory directly.

Runtime publishes copies.
Machine owns live state.

## Development Philosophy

Build vertically.

Prefer the smallest demonstrable machine slice.

Do not implement future phases early.

Do not add speculative abstractions.

## Phase Workflow

For each phase:

```text
1. Read STATUS.md.
2. Read HIGHLEVEL.md.
3. Read current phase document.
4. Implement only the documented phase.
5. Run tests.
6. Update STATUS.md.
```

## Definition Of Done

A phase is complete when:

```text
- Acceptance criteria pass.
- Existing tests continue to pass.
- Architecture rules remain intact.
- Thread ownership rules remain intact.
- STATUS.md reflects reality.
```
