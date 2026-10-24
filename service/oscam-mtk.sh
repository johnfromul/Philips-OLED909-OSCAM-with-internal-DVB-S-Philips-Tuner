#!/system/bin/sh
# Magisk service.d: the start command detaches and uses an exclusive file lock.
ROOT=/data/local/tmp/oscam-mtk
[ -e "$ROOT/disabled" ] && exit 0
[ -x "$ROOT/control.sh" ] && "$ROOT/control.sh" start
