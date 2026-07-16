# Vendored third-party code

These libraries are kept small and are intended to be wrapped by project-facing
APIs under `src/util/` before broad use.

## CIA timing corpus clones (not vendored into git)

`external/cia-timing-corpus/VICE-testprogs/` and
`external/cia-timing-corpus/c64ciaTests/` are fetched by
`tools/cia-timing-corpus/fetch.sh` and gitignored. Agent docs and VICE result
logs live under `md-files/corpus/cia-timing/`.

## Contents
- `VICE project's testprogs`
  - Upstream: <https://sourceforge.net/p/vice-emu/code/HEAD/tree/testprogs/CIA/>
  - License: Test programs sourced from the VICE project's testprogs repository,
     used here for CIA timing reference/validation only, not redistributed as
     part of c64m's build. VICE is licensed GPLv2; no separate license file
     accompanies the testprogs subtree.
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
