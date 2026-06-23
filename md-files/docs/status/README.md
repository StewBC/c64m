# c64m component status index

These files split the former monolithic `STATUS.md` into focused handoff documents.

Use this directory when a task only touches one subsystem. The goal is to avoid forcing every agent to consume all project history before making a targeted change.

Recommended reading pattern:

1. Read repository `AGENTS.md`.
2. Read repository `MASTER.md`.
3. Read top-level `STATUS.md`.
4. Read only the component status files relevant to the task.
5. Check `DEFERRED.md` before declaring a gap, regression, or future task.
6. Check `TESTING.md` before changing test expectations or smoke procedures.

Component ownership is intentionally practical, not pure hardware taxonomy. For example, SID register behavior lives in `SID.md`, while SDL output, buffering, recording, and sample scheduling live in `AUDIO.md`.
