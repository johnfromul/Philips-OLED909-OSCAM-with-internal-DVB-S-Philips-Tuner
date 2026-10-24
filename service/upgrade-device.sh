#!/system/bin/sh
# Upgrade only OSCam while keeping the Java CAS owner alive; see docs/operations.md.
set -eu
umask 077
ROOT=/data/local/tmp/oscam-mtk
NEW="$ROOT/oscam.next"
EXPECTED=$(cat "$NEW.sha256")
export TZ='CET-1CEST,M3.5.0,M10.5.0/3'
[ "$(id -u)" = 0 ]
[ "$(getprop ro.odm.tpvision.product.swver)" = TPM231WW_R.201.100.146.220 ]
[ -x "$NEW" ]
[ "$(sha256sum "$NEW" | cut -d ' ' -f1)" = "$EXPECTED" ]
"$NEW" -V | grep '^Version:.*2.26.09-11968@39e78a46'

read_pid() {
    value=$(cat "$ROOT/$1.pid")
    case "$value" in ''|*[!0-9]*) return 1;; esac
    kill -0 "$value"
    echo "$value"
}
watchdog=$(read_pid watchdog)
supervisor=$(read_pid supervisor)
old=$(read_pid oscam)
[ "$(readlink /proc/$old/exe)" = "$ROOT/oscam" ]
grep -q "$ROOT/watchdog.sh" "/proc/$watchdog/cmdline"
grep -q 'oscam-mtk-supervisor' "/proc/$supervisor/cmdline"

BACKUP="$ROOT/backups/pre-upgrade-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$ROOT/backups"
mkdir "$BACKUP"
cp -p "$ROOT/oscam" "$BACKUP/oscam.previous"
cp -p "$ROOT/oscam.version" "$BACKUP/oscam.version"
tar -cf "$BACKUP/config.tar" -C "$ROOT" config
find "$ROOT/config" -type f ! -name oscam.ccache -exec sha256sum '{}' \; > "$BACKUP/preserved.sha256"
sha256sum "$ROOT/mtk-bridge" "$ROOT/bridge-service.jar" \
    "$ROOT/control.sh" "$ROOT/watchdog.sh" /data/adb/service.d/oscam-mtk.sh >> "$BACKUP/preserved.sha256"
chmod 600 "$BACKUP/config.tar" "$BACKUP/preserved.sha256"
echo "BACKUP=$BACKUP"
success=0
swapped=0
cleanup() {
    status=$?
    set +e
    if [ "$swapped" = 1 ] && [ "$success" != 1 ]; then
        echo ROLLBACK_TO_PREVIOUS_BINARY
        current=$(cat "$ROOT/oscam.pid" 2>/dev/null)
        case "$current" in ''|*[!0-9]*) :;; *)
            [ "$(readlink "/proc/$current/exe" 2>/dev/null)" = "$ROOT/oscam" ] && kill -TERM "$current"
        esac
        count=0
        while kill -0 "${current:-0}" 2>/dev/null && [ "$count" -lt 8 ]; do
            sleep 1
            count=$((count+1))
        done
        cp -p "$BACKUP/oscam.previous" "$ROOT/oscam.rollback"
        mv "$ROOT/oscam.rollback" "$ROOT/oscam"
        "$ROOT/oscam" -b -c "$ROOT/config" -t "$ROOT" -B "$ROOT/oscam.pid"
    fi
    kill -CONT "$supervisor" 2>/dev/null
    kill -CONT "$watchdog" 2>/dev/null
    return "$status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
kill -STOP "$watchdog"
kill -STOP "$supervisor"
kill -TERM "$old"
count=0
while kill -0 "$old" 2>/dev/null; do
    [ "$count" -lt 12 ] || { echo OLD_OSCAM_STOP_TIMEOUT; exit 1; }
    sleep 1
    count=$((count+1))
done
mv "$NEW" "$ROOT/oscam"
swapped=1
"$ROOT/oscam" -b -c "$ROOT/config" -t "$ROOT" -B "$ROOT/oscam.pid"
count=0
while :; do
    if grep -q '^Version:.*2.26.09-11968' "$ROOT/oscam.version" && read_pid oscam >/dev/null 2>&1; then break; fi
    [ "$count" -lt 15 ] || { echo NEW_OSCAM_START_TIMEOUT; exit 1; }
    sleep 1
    count=$((count+1))
done
sha256sum -c "$BACKUP/preserved.sha256"
success=1
echo UPGRADE_11968_OK
