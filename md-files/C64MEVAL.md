# C64M Evaluation Plan

Purpose: perform a holistic, read-only evaluation of the `c64m` emulator codebase. The goal is a report, not fixes. The review should judge whether the code organization, complexity, ownership boundaries, and tests make sense for the problem being solved: a C64 emulator with frontend, runtime/debugging, tools, and platform support.

## Operating Rules For The Agent

- Do not modify source files.
- Do not reformat code.
- Do not perform speculative refactors.
- Prefer concrete observations over opinions.
- Quote file paths, function names, structs, enums, and APIs where relevant.
- Distinguish between:
  - confirmed findings,
  - plausible concerns,
  - open questions,
  - improvement suggestions.
- When possible, support observations with evidence from the code.
- Keep a running notes file or final report draft, but do not edit the project source.
- Treat emulator-domain complexity as potentially legitimate; do not classify large files as bad solely because they are large.
- Focus on whether complexity is inherent to C64 emulation or accidental glue complexity.

## Suggested Output

Produce a final report with these sections:

1. Executive summary
2. Overall architecture and layering
3. Ownership of time, state, and control flow
4. File/module-level observations
5. Complexity hotspots
6. Consistency findings
7. Emulator-domain fit
8. Test alignment
9. Recommendations
10. Open questions

Each recommendation should be tagged as one of:

- `leave alone`
- `document`
- `low-risk cleanup`
- `medium-risk refactor`
- `high-risk refactor`
- `needs more investigation`

The report should be written to the md-files/notes/ folder using the format `C64EVAL_PHASE_[#]` where `[#]` is the phase number.

## Phase 0 - Repository Survey

Goal: establish a factual map before judging anything.

Tasks:

- Count source lines by directory and file type.
- Identify largest files, largest headers, and generated/vendor files.
- Identify build targets from CMake files.
- Identify test directories, test targets, test naming conventions, and test coverage shape.
- Identify third-party/vendor code that should be excluded from maintainability judgments.

Likely vendor/generated files to exclude from ordinary source-health scoring:

- `frontend/nuklear.h`
- `frontend/c64_pro_mono_font_data.h`
- generated files, if any
- generated parser/perfect-hash outputs, if confirmed generated

Questions to answer:

- What are the real project-owned source line counts after excluding vendor/generated files?
- Which files dominate the project-owned code size?
- Which directories appear to be libraries versus app-only modules?
- Are tests present for the major code areas?

Deliverable:

- Short repository map.
- Table of largest project-owned files.
- Initial risk ranking by size and responsibility.

## Phase 1 - Layering And Dependency Direction

Goal: determine whether the architecture has clean boundaries.

Expected high-level layers:

```text
main/app_options
    app lifecycle, CLI/config, top-level orchestration

frontend/
    UI, layout, input mapping, debugger display, help, Nuklear integration

runtime/
    execution thread, run/pause/step, commands, events, breakpoints, client API

machine/
    deterministic emulated C64 hardware model

platform/
    host platform abstraction, audio, OS/SDL integration

tools/
    assembler, D64, disassembler, symbols

util/
    reusable primitives: thread, mutex, cond, queue, config, audio buffer
```

Preferred dependency direction:

```text
frontend -> runtime -> machine
main     -> frontend/runtime/platform/app_options
platform -> util and host APIs
machine  -> util only, ideally no frontend/platform/runtime knowledge
tools    -> mostly independent, reusable where useful
util     -> no project-specific higher-level dependencies
```

Tasks:

- Inspect includes across directories.
- Identify upward or sideways dependencies.
- Identify whether `machine/` is isolated from frontend/runtime/platform concerns.
- Identify whether `runtime/` exposes a narrow API or leaks internal machine/frontend details.
- Identify whether `frontend/` directly mutates emulator internals or uses runtime APIs.
- Identify whether `main.c` is orchestration only or contains business logic.

Questions to answer:

- Is there a clear dependency hierarchy?
- Are any modules cyclically dependent in practice?
- Are public headers small and intentional?
- Does any layer know too much about another layer?

Deliverable:

- Dependency/layering summary.
- List of clean boundaries.
- List of suspicious boundary crossings.

## Phase 2 - Ownership Of Time, State, And Control

Goal: answer the most important emulator architecture question: who owns time?

Tasks:

- Identify the core execution loop.
- Identify where CPU cycles, chip stepping, frame generation, audio generation, and host pacing are coordinated.
- Identify where run/pause/step state lives.
- Identify where breakpoints are checked.
- Identify how frontend commands reach the emulator.
- Identify whether debugger inspection can perturb machine state.
- Identify whether the runtime thread owns only scheduling or also emulator semantics.

Questions to answer:

- Who owns machine time?
- Who owns frame timing?
- Who owns audio timing?
- Who owns run/pause/step state?
- Who owns debugger state?
- Who is allowed to mutate the emulated machine?
- Are these ownership rules obvious from the code?

Files likely relevant:

- `runtime/runtime_thread.c`
- `runtime/runtime_client.c`
- `runtime/runtime_command.h`
- `runtime/runtime_event.h`
- `runtime/runtime_internal.h`
- `machine/c64.c`
- `machine/c64.h`
- `machine/c64_bus.c`
- `frontend/frontend.c`
- `main.c`

Deliverable:

- Ownership model diagram or prose equivalent.
- List of single sources of truth.
- List of duplicated or unclear state ownership.
- Specific risks around determinism, stepping, synchronization, or debugger interaction.

## Phase 3 - Machine Core Review

Goal: evaluate whether the emulator core is organized in a way that fits C64 emulation.

Tasks:

- Review the machine-level module boundaries:
  - `c64.c/.h`
  - `c64_bus.c/.h`
  - `c6510.c/.h`
  - `c6510_inln.h`
  - `cia.c/.h`
  - `vicii.c/.h`
  - `sid.c/.h`
  - `keyboard.c/.h`
  - `c64_rom.c/.h`
- Identify state structs and ownership.
- Identify chip stepping APIs.
- Identify bus/memory mapping responsibilities.
- Identify whether timing assumptions are localized or spread around.
- Identify whether public machine headers expose too much mutable state.

Questions to answer:

- Is `machine/` deterministic and mostly host-independent?
- Is the C64 hardware model decomposed naturally?
- Does `c64.c` act as a clean coordinator or a dumping ground?
- Is bus behavior centralized?
- Are CPU/VIC-II/CIA interactions easy to reason about?
- Are ROM, keyboard, frame, and SID concerns appropriately separated?

Deliverable:

- Machine-core architecture summary.
- Correctness-sensitive hotspots.
- Maintainability-sensitive hotspots.
- Suggested documentation points for timing and ownership.

## Phase 4 - Runtime And Debugger Review

Goal: evaluate the runtime layer as the bridge between deterministic emulation and interactive use.

Tasks:

- Inspect command and event models.
- Inspect breakpoint handling.
- Inspect client API boundaries.
- Inspect thread lifecycle and synchronization.
- Identify direct use of mutexes, condvars, queues, and shared state.
- Identify whether runtime thread code combines too many responsibilities.
- Identify whether runtime code is testable without frontend.

Files likely relevant:

- `runtime/runtime_thread.c`
- `runtime/runtime_client.c`
- `runtime/runtime.c`
- `runtime/runtime.h`
- `runtime/runtime_internal.h`
- `runtime/runtime_command.h`
- `runtime/runtime_event.h`
- `runtime/runtime_breakpoint_ini.c`
- `util/message_queue.c`
- `util/thread.c`
- `util/mutex.c`
- `util/cond.c`

Questions to answer:

- Is there a clean state machine for runtime execution?
- Are command/event types coherent and minimal?
- Are pause/run/step/breakpoint semantics centralized?
- Are there possible race-prone areas?
- Is `runtime_thread.c` large because it is doing one complex thing, or many unrelated things?

Deliverable:

- Runtime responsibility map.
- Synchronization risk list.
- State-machine clarity assessment.
- Suggestions for documentation or future decomposition.

## Phase 5 - Frontend Review

Goal: evaluate whether frontend complexity is contained and appropriate.

Tasks:

- Inspect `frontend/frontend.c` at a high level.
- Identify UI subfeatures inside the file.
- Identify state structs and ownership.
- Identify interactions with runtime and machine.
- Identify input mapping path.
- Identify layout responsibilities.
- Identify Nuklear integration boundaries.
- Identify whether frontend has business logic that belongs in runtime or machine.

Files likely relevant:

- `frontend/frontend.c`
- `frontend/frontend.h`
- `frontend/frontend_input.c`
- `frontend/c64_layout.c`
- `frontend/help_view.c`
- `frontend/nuklear_impl.c`
- `frontend/nuklear_sdl.h`
- `frontend/nuklear_config.h`

Questions to answer:

- Is the frontend mostly presentation, or does it own emulator policy?
- Is debugger UI state separated from emulator state?
- Is input mapping isolated and understandable?
- Is Nuklear kept behind a small boundary, or spread everywhere?
- Would adding a new debugger panel be straightforward?

Deliverable:

- Frontend feature map.
- UI/state ownership assessment.
- Suggestions for future splitting, if justified.

## Phase 6 - App Options, Main, Platform, And Util Review

Goal: evaluate startup, configuration, host abstraction, and reusable primitives.

Tasks:

- Inspect `main.c` for orchestration versus embedded policy.
- Inspect `app_options.c/.h` for option parsing, defaults, validation, and ownership.
- Inspect `platform/` for host-specific leakage.
- Inspect `util/` for generality and consistency.
- Identify whether defaults are duplicated between CLI, config, frontend, and runtime.
- Identify whether errors are reported consistently.

Files likely relevant:

- `main.c`
- `app_options.c`
- `app_options.h`
- `platform/platform.c`
- `platform/platform_audio.c`
- `util/config.c`
- `util/audio_buffer.c`
- `util/message_queue.c`

Questions to answer:

- Is startup/shutdown sequencing clear?
- Are app options validated in one place?
- Are config defaults centralized?
- Is platform code narrow and replaceable?
- Are util modules genuinely reusable, or project-specific?

Deliverable:

- Lifecycle/config/platform summary.
- Duplicated default or validation findings.
- Error-handling consistency findings.

## Phase 7 - Tools Review

Goal: evaluate whether tools are cleanly isolated and proportionate.

Tasks:

- Review assembler architecture.
- Review D64 library.
- Review 6502 disassembler.
- Review symbols library.
- Identify shared concepts with the main emulator.
- Identify whether tools are reusable libraries or app-coupled code.
- Identify whether parser/generated files should be treated specially.

Files likely relevant:

- `tools/assembler/parse.c`
- `tools/assembler/asm.c`
- `tools/assembler/expr.c`
- `tools/assembler/opcode.c`
- `tools/assembler/symbol.c`
- `tools/d64/d64.c`
- `tools/disasm_6502/disasm_6502.c`
- `tools/symbols/symbol_table.c`

Questions to answer:

- Is the assembler complexity appropriate?
- Are parse, expression, symbol, emit, and opcode responsibilities clear?
- Are disassembler and symbol-table APIs useful outside the frontend?
- Are tool errors reported consistently?

Deliverable:

- Tools architecture summary.
- Parser/assembler complexity assessment.
- Reuse and coupling findings.

## Phase 8 - Consistency Sweep

Goal: inspect cross-cutting consistency across the project.

Tasks:

Review consistency in:

- naming conventions,
- file naming,
- public/private API shape,
- struct ownership,
- init/free patterns,
- error return conventions,
- logging/reporting conventions,
- memory ownership rules,
- include patterns,
- use of assertions,
- comments and invariants,
- use of `static`,
- use of inline/header implementation,
- threading primitives,
- configuration/default handling.

Questions to answer:

- Are APIs predictable across modules?
- Are ownership/lifetime rules obvious?
- Are errors handled consistently?
- Are large functions large for good reasons?
- Are comments explaining why, or merely repeating what?

Deliverable:

- Consistency findings grouped by theme.
- Examples of good patterns worth preserving.
- Examples of inconsistent patterns worth standardizing.

## Phase 9 - Test Alignment Review

Goal: determine whether tests match the project's risk profile.

Tasks:

- Identify all tests and test targets.
- Categorize tests by subsystem.
- Compare test coverage shape to code risk shape.
- Identify deterministic emulator tests versus UI/runtime smoke tests.
- Identify flaky-risk tests, especially sleep/timing/thread tests.
- Identify missing high-value tests.

High-value emulator test areas:

- CPU instructions,
- CPU flags,
- addressing modes,
- interrupt behavior,
- bus mapping,
- bank switching,
- CIA timers,
- keyboard matrix,
- VIC-II raster/frame timing,
- ROM loading and failure paths,
- run/pause/step behavior,
- breakpoints,
- assembler parsing,
- assembler expressions,
- D64 parsing,
- config/options parsing.

Questions to answer:

- Do tests cover the code most likely to break?
- Are there integration tests that run small C64 programs?
- Are there golden-state, golden-frame, or trace-based tests?
- Is runtime-thread behavior tested deterministically?
- Are tools tested separately from app integration?

Deliverable:

- Test coverage map.
- Risk/test mismatch list.
- Suggested high-value tests.

## Phase 10 - Final Synthesis

Goal: convert observations into a useful, prioritized report.

Tasks:

- Identify top strengths.
- Identify top risks.
- Identify areas that are large but probably appropriate.
- Identify areas that are large because responsibilities are mixed.
- Separate correctness risks from maintainability risks.
- Separate near-term recommendations from long-term architectural suggestions.
- Avoid broad prescriptions without code evidence.

Suggested recommendation format:

```text
Recommendation: <short title>
Category: document | low-risk cleanup | medium-risk refactor | high-risk refactor | leave alone | needs more investigation
Area: <files/modules>
Evidence: <specific code observations>
Why it matters: <correctness, maintainability, testability, determinism, etc.>
Suggested direction: <what to consider, not an implementation patch>
Risk of acting: <low/medium/high>
Risk of not acting: <low/medium/high>
```

Final report should answer:

- Does the repo organization make sense for a C64 emulator?
- Where is complexity inherent and justified?
- Where is complexity accidental?
- Are state and timing ownership clear?
- Are boundaries between machine/runtime/frontend/platform/tools healthy?
- Are tests aligned with risk?
- What should be left alone?
- What should be documented?
- What might be worth improving later?

## Recommended Execution Strategy

Run this as multiple passes, not one giant pass.

Recommended order:

1. Phase 0: survey
2. Phase 1: layering
3. Phase 2: ownership of time/state/control
4. Phase 3: machine core
5. Phase 4: runtime/debugger
6. Phase 5: frontend
7. Phase 6: main/options/platform/util
8. Phase 7: tools
9. Phase 8: consistency
10. Phase 9: tests
11. Phase 10: synthesis

Use one agent run per phase when possible. Each phase should produce notes that the final synthesis phase can consume.

For model settings, use the highest reasoning/thinking setting available for phases 1, 2, 3, 4, 8, 9, and 10. These phases involve architecture, ownership, concurrency, emulator correctness, and synthesis. Lower or normal reasoning is acceptable for Phase 0 and simple inventory work, but high reasoning is still safe if cost/time is acceptable.

