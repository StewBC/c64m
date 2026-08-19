# am65

`am65` is the assembler used by a2m and by the standalone `am65` command-line
program. It is designed to become the shared source of truth for c64m as well.
The directory is self-contained so it can be maintained as a Git subtree in
each consumer.

The initial CPU profile is NMOS 6502. It can be selected through the library
API (`assembler_set_cpu_profile`), with `am65 --cpu`, or changed within source:

| Directive | Accepted instructions |
|---|---|
| `.6502` | Portable documented NMOS 6502 set |
| `.65c02` | Core WDC/Rockwell-compatible 65C02 additions |
| `.rockwell` | 65C02 plus RMB/SMB and BBR/BBS bit operations |
| `.wdc` | Rockwell profile plus WAI and STP |

Profiles are cumulative. Selecting a profile establishes the initial state for
each assembly; an in-source directive affects subsequent lines. This lets c64m
and Apple ][+ select 6502, Apple //e Enhanced select 65C02, and standalone users
opt into Rockwell or WDC instructions explicitly.

Regenerate `gperf.c` after changing `gperf.gperf`:

```sh
gperf --language=ANSI-C -c --output-file=gperf.c gperf.gperf
```
