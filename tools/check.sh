#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$ROOT/build"
"${HOST_CC:-clang}" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/src/protocol.c" "$ROOT/tests/protocol_test.c" -o "$ROOT/build/protocol-test"
"$ROOT/build/protocol-test"
for file in "$ROOT"/tools/*.sh "$ROOT"/service/*.sh "$ROOT/tools/arm64-cc"; do
    sh -n "$file"
done
python3 - "$ROOT" <<'PY'
import json,re,sys
from pathlib import Path
root=Path(sys.argv[1]);json.loads((root/'docs/tested-firmware.json').read_text())
for doc in [root/'README.md',*(root/'docs').glob('*.md')]:
 for link in re.findall(r'\]\(([^)]+)\)',doc.read_text()):
  if '://' in link or link.startswith('#') or link.startswith('mailto:'):continue
  target=link.split('#',1)[0]
  if target and not (doc.parent/target).exists():raise SystemExit(f'Broken relative link in {doc.name}: {target}')
print('Shell syntax, firmware metadata and documentation links passed.')
PY
