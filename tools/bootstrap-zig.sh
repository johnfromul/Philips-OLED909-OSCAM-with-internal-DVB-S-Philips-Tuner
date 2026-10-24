#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -x "$ROOT/tools/zig/zig" ]; then
    [ "$("$ROOT/tools/zig/zig" version)" = 0.15.2 ]
    exit 0
fi
[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || {
    echo 'This bootstrap is for Apple Silicon macOS. Install Zig 0.15.2 and set ZIG_EXECUTABLE on other hosts.' >&2
    exit 1
}
mkdir -p "$ROOT/.cache"
ARCHIVE="$ROOT/.cache/zig-aarch64-macos-0.15.2.tar.xz"
curl --fail --location --output "$ARCHIVE" https://ziglang.org/download/0.15.2/zig-aarch64-macos-0.15.2.tar.xz
python3 - "$ARCHIVE" <<'PY'
import hashlib,sys
from pathlib import Path
actual=hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()
expected='3cc2bab367e185cdfb27501c4b30b1b0653c28d9f73df8dc91488e66ece5fa6b'
if actual!=expected:raise SystemExit('Zig archive checksum mismatch')
PY
tar -xf "$ARCHIVE" -C "$ROOT/.cache"
mv "$ROOT/.cache/zig-aarch64-macos-0.15.2" "$ROOT/tools/zig"
"$ROOT/tools/zig/zig" version
