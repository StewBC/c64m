<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_A.md
# D64 Parser and PRG Extraction

## Goal

Add a tools-level D64 parser that can inspect common 35-track `.d64` disk images, enumerate directory entries, and extract PRG file data without coupling to the emulator runtime, frontend, or live machine.

This phase must prove that disk image bytes can be parsed safely before any KERNAL trap, runtime mount state, or UI is added.

## Scope

Implement a small, owned parser under the tools layer. The exact file names are up to the implementation agent after inspecting the current source tree. Likely shape:

```text
src/tools/d64/...
tests/tools/test_d64...
```

The parser must support:

- Standard 35-track 174848-byte D64 images.
- Track/sector to byte-offset conversion for tracks 1 through 35.
- BAM sector inspection sufficient to read disk title, disk ID, DOS type, and free-block count if available.
- Directory sector chain starting from track 18, sector 1.
- Directory entries with file type, filename bytes, first data track/sector, and block count.
- PRG file extraction by following the file track/sector chain.
- The two-byte little-endian PRG load address as part of the extracted PRG bytes.
- Safe failure for malformed images and bad chains.

## Non-goals

Do not implement:

- Runtime mount state.
- Machine-owned drive slots.
- KERNAL trap behavior.
- Directory BASIC program synthesis.
- Frontend UI.
- D64 mutation or save support.
- 1541 CPU/ROM emulation.
- IEC bus timing.
- Fast-loader behavior.
- GCR decoding.
- 40-track, 42-track, or error-info D64 variants.

## Architecture rules

The parser is tools-level code. It must not depend on frontend, runtime, platform, or machine state.

Allowed direction:

```text
tools -> util
```

Do not introduce a dependency from tools to machine or runtime.

## Data model guidance

Use plain owned C structs. Keep names consistent with project style after inspecting existing tools code.

Suggested concepts:

```c
struct d64_image;
struct d64_directory_entry;
struct d64_file_data;
struct d64_error;
```

The parser should preserve raw PETSCII filename bytes from the directory entry. It may also expose a helper that converts ordinary PETSCII filenames to an ASCII debug/display string for tests and UI status, but the raw bytes remain authoritative.

Normal Commodore filenames should be represented as PETSCII directory bytes. Tests may compare against normalized uppercase ASCII renderings such as `MENU1`, `LAKESPT.BIN`, and `LAKESTR.TXT` for readability.

## D64 geometry

Implement standard 35-track geometry explicitly.

Recommended sector counts:

```text
Tracks  1-17: 21 sectors each
Tracks 18-24: 19 sectors each
Tracks 25-30: 18 sectors each
Tracks 31-35: 17 sectors each
```

Each sector is 256 bytes.

Reject track 0, sector numbers outside the selected track's sector count, and sector offsets beyond the image length.

## Directory parsing

Directory sectors contain eight 32-byte entries after the first two bytes of each sector, which point to the next directory sector.

For each entry, parse at least:

```text
byte  0: file type and flags
byte  1: first file track
byte  2: first file sector
bytes 3-18: filename, PETSCII padded with 0xA0
bytes 30-31: block count, little-endian
```

Skip deleted/empty entries when enumerating normal visible entries. Preserve unsupported file types as directory metadata, but do not extract them as PRG files in this phase.

Directory traversal must stop safely when the next track is 0. It must detect loops and invalid next sectors.

## PRG extraction

For PRG entries:

- Follow the track/sector chain from the entry's first track/sector.
- For intermediate sectors, bytes 2 through 255 are file data.
- For final sectors, byte 0 is zero and byte 1 gives the number of used bytes in the final sector. Interpret the final used-byte count carefully and write tests around it.
- Detect loops, out-of-range sectors, and chains that exceed a conservative maximum.
- Return the complete PRG byte stream, including the two-byte load address.
- Reject extracted PRG files shorter than two bytes.

Do not load bytes into emulator memory in this phase.

## Fixtures

Use both fixture styles:

1. Committed known-good images:

```text
assets/disks/ODELLLAK.D64
assets/disks/blank.d64
```

2. Generated tiny test images for exact edge cases:

- Minimal valid directory with one PRG.
- Empty formatted disk.
- Bad directory next-sector pointer.
- File chain loop.
- File chain out-of-range sector.
- PRG shorter than two bytes.

`ODELLLAK.D64` is expected to contain ordinary Commodore directory names including:

```text
ASS PRESENTS:
ODELL LAKE
MENU1
LAKESPT.BIN
LAKE1.HRS
LAKESTR.TXT
GENINFO.TXT
SSTACK.OBJ
LAKE1.FNT
LOGOMOVE.OBJ
LOGO.SPT
SCP3.OBJ
INPUT3.OBJ
LAKE1.TEX
LAKE1.COL
FISHDXY.TXT
STRFLP.OBJ
LAKESPTPNT.BIN
MHTNT.OBJ
MSIRQ.OBJ
REMROS.OBJ
LAKESPTCOL.BIN
LAKERASDAT.BIN
SCNUM.OBJ
LAKESPTREC.BIN
SPTEAT.OBJ
HOOK.SPT
NET.SPT
DISPATCH.OBJ
TTFER.OBJ
```

The fixture names are ordinary PETSCII/C64 names. Do not add special Scandinavian-character behavior based on OCR errors.

## Error handling

Return explicit status values rather than crashing or returning partial success silently.

Required failures:

- Wrong image size for this phase.
- Track out of range.
- Sector out of range.
- Directory chain loop.
- File chain loop.
- Unsupported file type extraction.
- PRG shorter than two bytes.
- Allocation failure.

Keep diagnostics useful but do not invent a 1541 error channel in this tools phase.

## Acceptance criteria

- A standard 35-track D64 image is accepted.
- Non-35-track variants are rejected with a clear unsupported-image result.
- `blank.d64` parses as a valid disk with no ordinary files.
- `ODELLLAK.D64` parses and enumerates the expected directory names and file types.
- `MENU1` can be found as a PRG and extracted with at least two bytes of file data.
- Unsupported file types such as SEQ entries are listed but PRG extraction rejects them.
- Generated malformed images fail safely.
- Directory and file chain loop tests terminate deterministically.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- D64 parser phase complete.
- Supported image format: standard 35-track D64 only.
- Directory enumeration and PRG extraction status.
- Fixture coverage added.
- Remaining non-goals: runtime mount, KERNAL trap, directory load, disk writes, 1541/IEC/fast loaders.
