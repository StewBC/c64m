# Timing baseline fixtures

This directory holds project-owned timing baselines used while c64m's CPU/VIC-II
bus implementation changes.

These records are regression baselines, not claims that every listed behavior is
verified against a particular VIC-II chip revision. A baseline becomes a
hardware-validated expectation only when its source and target revision are named.

The executable assertions remain in `tests/machine/`. The documents make their
fixture setup, coverage, and limitations reviewable without reading test code.
