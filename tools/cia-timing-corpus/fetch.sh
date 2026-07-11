#!/usr/bin/env bash
# Fetch VICE CIA testprogs + hardware-result repo into external/cia-timing-corpus/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/external/cia-timing-corpus"
mkdir -p "$DEST"
cd "$DEST"

if [[ ! -d VICE-testprogs/.git ]]; then
  git clone --depth 1 https://github.com/libsidplayfp/VICE-testprogs.git
else
  echo "VICE-testprogs already present"
fi

if [[ ! -d c64ciaTests/.git ]]; then
  git clone --depth 1 https://github.com/dmolinagarcia/c64ciaTests.git
else
  echo "c64ciaTests already present"
fi

echo "OK: sources under $DEST"
