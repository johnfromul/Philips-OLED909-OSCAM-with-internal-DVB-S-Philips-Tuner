#!/bin/sh
set -eu
umask 077
[ "$#" = 1 ] || { echo 'usage: upgrade.sh TV_ADB_ENDPOINT' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ADB_BIN=${ADB:-adb}
DEVICE=$1
[ -f "$ROOT/build/oscam-arm64" ]
python3 - "$ROOT/build/oscam-arm64" "$ROOT/build/oscam-arm64.sha256" <<'PY'
import hashlib,sys
from pathlib import Path
Path(sys.argv[2]).write_text(hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()+'\n')
PY
STAGE=/data/local/tmp/oscam-mtk-upgrade-$(date +%s)
"$ADB_BIN" -s "$DEVICE" shell "mkdir '$STAGE' && chmod 700 '$STAGE'"
"$ADB_BIN" -s "$DEVICE" push "$ROOT/build/oscam-arm64" "$ROOT/build/oscam-arm64.sha256" "$ROOT/service/upgrade-device.sh" "$STAGE/"
"$ADB_BIN" -s "$DEVICE" shell "su -c 'cp $STAGE/oscam-arm64 /data/local/tmp/oscam-mtk/oscam.next; cp $STAGE/oscam-arm64.sha256 /data/local/tmp/oscam-mtk/oscam.next.sha256; chmod 700 /data/local/tmp/oscam-mtk/oscam.next; chmod 600 /data/local/tmp/oscam-mtk/oscam.next.sha256; sh $STAGE/upgrade-device.sh'"
