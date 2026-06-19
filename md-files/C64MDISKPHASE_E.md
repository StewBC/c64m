<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_E.md
# Filename Matching, Wildcards, and Load Errors

## Goal

Improve filename matching and failure behavior for mounted D64 loads so ordinary C64 BASIC usage is practical and predictable.

This phase tightens edge cases after exact PRG loads and directory loading already work.

## Scope

Implement and test:

- Byte-preserving PETSCII filename handling internally.
- Normalized comparison for ordinary C64 names.
- `LOAD "*",8` and `LOAD "*",8,1` selecting the first loadable PRG.
- Prefix wildcard such as `LOAD "GAME*",8`.
- `?` single-character wildcard if it can be implemented cleanly.
- Stable behavior for unsupported types and missing files.
- Clear internal diagnostics for load failure.

## Non-goals

Do not implement:

- Full Commodore DOS pattern semantics beyond the documented subset.
- File type suffix parsing such as `,P,R` unless a test fixture requires it and the behavior is documented.
- Disk command channel.
- 1541 error channel.
- SAVE or disk mutation.
- Device 9 unless this is chosen as the optional final disk phase.

## Filename model

Parser-level directory names are raw PETSCII bytes padded with `0xA0`. Keep those bytes preserved.

Comparison helpers may expose normalized forms for ordinary names:

- Trim trailing PETSCII shifted spaces/padding.
- Compare ordinary ASCII letters case-insensitively only as a host convenience if it does not break C64 behavior.
- Preserve punctuation, digits, and spaces.
- Do not introduce special-character mappings unless backed by a real fixture or test.

Document the selected comparison rules in code comments and STATUS.md.

## Wildcard subset

Required:

```text
*       matches any suffix
NAME*   prefix match
?       one character, if implemented in this phase
```

Recommended behavior:

- `LOAD "*",8,1` loads the first visible PRG entry in directory order.
- `LOAD "*",8` does the same but with BASIC-load semantics.
- Wildcards should skip unsupported file types for PRG loads unless the user explicitly asks for a type that later phases support.
- Directory load `$` remains special and must not be treated as a filename wildcard.

Deferred:

- Full DOS pattern rules.
- File type suffixes.
- Directory partition or command syntax.

## Error behavior

Tighten failure paths introduced earlier. Required failures:

- No disk mounted.
- Unsupported image/mount state.
- Missing filename.
- Unsupported file type.
- Chain loop.
- Out-of-range track/sector.
- Target memory overflow.
- Unsupported device/mode.

On failure:

- Do not crash.
- Do not corrupt unrelated emulator state.
- Return KERNAL-style failure sufficient for BASIC to report failure or continue normally.
- Record enough debug/log context for maintainers.

Do not add a full 1541 error channel in this phase.

## Tests

Required tests:

- Exact match still works for `MENU1`.
- Lowercase/uppercase host convenience works only according to documented policy.
- `LOAD "*",8,1` loads the first PRG from `ODELLLAK.D64` according to directory order.
- Prefix wildcard such as `LAKE*` resolves deterministically.
- `?` wildcard works if included, or is explicitly deferred and tested as unsupported.
- Wildcard matching skips SEQ entries for PRG loads where appropriate.
- Missing wildcard match fails safely.
- Missing exact file fails safely.
- Unsupported file type fails safely.
- Chain-loop and out-of-range generated fixtures fail safely.
- Directory load `$` still works.
- Existing PRG load and host PRG loader behavior still work.

## Acceptance criteria

- Ordinary C64 filename matching is reliable for exact and documented wildcard cases.
- Load failures are deterministic and safe.
- The behavior is documented enough that later full-DOS-pattern work can extend it without guessing.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- Disk Phase E complete.
- Exact filename and wildcard subset implemented.
- Error behavior tightened.
- Tests added.
- Remaining gaps: full DOS pattern semantics, file type suffix parsing if deferred, device 9 if deferred, D64 writes, error channel, 1541/IEC/fast loaders.
