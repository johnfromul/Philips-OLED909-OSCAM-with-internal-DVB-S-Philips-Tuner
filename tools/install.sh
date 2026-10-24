#!/bin/sh
set -eu
umask 077
[ "$#" = 2 ] || { echo 'usage: install.sh TV_ADB_ENDPOINT PRIVATE_CONFIG_DIRECTORY' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PRIVATE_CONFIG=$(CDPATH= cd -- "$2" && pwd)
case "$PRIVATE_CONFIG" in "$ROOT"|"$ROOT"/*) echo 'Keep private configuration outside the repository.' >&2; exit 1;; esac
ADB_BIN=${ADB:-adb}
DEVICE=$1
for file in oscam.conf oscam.server oscam.user; do [ -f "$PRIVATE_CONFIG/$file" ]; done
if grep -qE 'CHANGE_ME_|READER_USERNAME|READER_PASSWORD|reader\.example\.invalid' \
    "$PRIVATE_CONFIG/oscam.conf" "$PRIVATE_CONFIG/oscam.server"; then
    echo 'Replace configuration placeholders before installation.' >&2; exit 1
fi
for file in oscam-arm64 mtk-bridge bridge-service.jar; do [ -f "$ROOT/build/$file" ]; done
"$ADB_BIN" -s "$DEVICE" shell "su -c 'test ! -e /data/local/tmp/oscam-mtk && test ! -e /data/adb/service.d/oscam-mtk.sh'"
STAGE=/data/local/tmp/oscam-mtk-install-$(date +%s)
"$ADB_BIN" -s "$DEVICE" shell "mkdir '$STAGE' && chmod 700 '$STAGE' && mkdir '$STAGE/config' && chmod 700 '$STAGE/config'"
"$ADB_BIN" -s "$DEVICE" push "$ROOT/build/oscam-arm64" "$ROOT/build/mtk-bridge" "$ROOT/build/bridge-service.jar" \
    "$ROOT/service/control.sh" "$ROOT/service/watchdog.sh" "$ROOT/service/oscam-mtk.sh" "$ROOT/service/install-device.sh" "$STAGE/"
"$ADB_BIN" -s "$DEVICE" push "$PRIVATE_CONFIG/oscam.conf" "$PRIVATE_CONFIG/oscam.server" "$PRIVATE_CONFIG/oscam.user" "$STAGE/config/"
"$ADB_BIN" -s "$DEVICE" shell "su -c 'chmod 600 $STAGE/config/oscam.conf $STAGE/config/oscam.server $STAGE/config/oscam.user; sh $STAGE/install-device.sh $STAGE'"
