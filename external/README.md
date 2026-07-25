# Vendored third-party code

These libraries are kept small and are intended to be wrapped by project-facing
APIs under `src/util/` before broad use.

## Optional: CIA timing corpus (not in git)

A clean clone of this repository does **not** include anything under
`external/cia-timing-corpus/`. Those directories are gitignored and are never
fetched by CMake, the normal build, or `ctest`.

If they appear on disk, someone ran `tools/cia-timing-corpus/fetch.sh` (or
cloned the upstreams there by hand). That is a manual development path for
race-level CIA comparison against VICE/hardware test programs — not part of
everyday build or test. Safe to delete at any time (`rm -rf
external/cia-timing-corpus/`); re-run `fetch.sh` only when you need the oracle
scripts under `tools/cia-timing-corpus/`.

When present, typical contents are:

- `VICE-testprogs/` — full multi-system VICE testprog tree  
  Upstream: <https://github.com/libsidplayfp/VICE-testprogs>  
  (CIA-relevant paths only are used by the run scripts.)  
  License: VICE GPLv2; used for local reference/validation only, not
  redistributed as part of c64m’s product build.
- `c64ciaTests/` — hardware dump / result notes  
  Upstream: <https://github.com/dmolinagarcia/c64ciaTests>

Agent notes and any saved run logs live under `md-files/corpus/cia-timing/`
(also optional evidence, not a ctest gate).

## Contents (vendored in git)

- `C64_TrueType_v1.2.1-STYLE`
  - Upstream: <http://style64.org/c64-truetype>
  - License: http://style64.org/c64-truetype/license
- `stb/stb_ds.h`
  - Upstream: <https://github.com/nothings/stb>
  - License: public domain or MIT
- `inih/ini.c`, `inih/ini.h`
  - Upstream: <https://github.com/benhoyt/inih>
  - License: BSD-3-Clause
- `logc/log.c`, `logc/log.h`
  - Upstream: <https://github.com/rxi/log.c>
  - License: MIT
- `argparse/argparse.c`, `argparse/argparse.h`
  - Upstream: <https://github.com/cofyc/argparse>
  - License: MIT
- `whereami/whereami.c`, `whereami/whereami.h`
  - Upstream: <https://github.com/gpakosz/whereami>
  - License: MIT or WTFPL v2
- `tiny-regex-c/re.c`, `tiny-regex-c/re.h`
  - Upstream: <https://github.com/kokke/tiny-regex-c>
  - License: The Unlicense (public domain)
