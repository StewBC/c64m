# Timing baseline fixtures

This directory holds project-owned timing baselines used while c64m's CPU/VIC-II
bus implementation changes.

These records are regression baselines, not claims that every listed behavior is
verified against a particular VIC-II chip revision. A baseline becomes a
hardware-validated expectation only when its source and target revision are named.

The executable assertions remain in `tests/machine/`. The documents make their
fixture setup, coverage, and limitations reviewable without reading test code.

## CPU migrated-family contention gate

`test_c64_cpu_validation` runs representative newly migrated documented
addressing families against the normal trace and the PAL bad-line fixture
(raster `$33`, cycle 12). It compares opcode PC, CPU state, memory result, and
every bus-event field except absolute time; each contended event must retain the
normal trace shape and occur no earlier, with at least one event delayed by BA.

The matrix covers absolute, zero-page-indexed, absolute-indexed, `(zp,X)`,
`(zp),Y`, `BIT`, indexed RMW, indirect-Y store, and NMOS indirect-JMP. It is a
current-model regression gate, not a hardware-authoritative trace suite.
