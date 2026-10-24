#!/system/bin/sh
set -u
umask 077
ROOT=/data/local/tmp/oscam-mtk
export TZ='CET-1CEST,M3.5.0,M10.5.0/3'
export CLASSPATH="$ROOT/bridge-service.jar"
echo $$ > "$ROOT/watchdog.pid"
supervisor_pid=

oscam_pid() {
    candidate=$(cat "$ROOT/oscam.pid" 2>/dev/null) || return 1
    case "$candidate" in ''|*[!0-9]*) return 1;; esac
    [ "$(cat "/proc/$candidate/comm" 2>/dev/null)" = oscam ] || return 1
    [ "$(readlink "/proc/$candidate/exe" 2>/dev/null)" = "$ROOT/oscam" ] || return 1
    kill -0 "$candidate" 2>/dev/null || return 1
    echo "$candidate"
}
cleanup() {
    trap '' TERM INT
    if [ -n "$supervisor_pid" ]; then
        kill -TERM "$supervisor_pid" 2>/dev/null
        wait "$supervisor_pid" 2>/dev/null
    fi
    process=$(oscam_pid) && kill -TERM "$process" 2>/dev/null
    rm -f "$ROOT/watchdog.pid" "$ROOT/supervisor.pid"
}
trap 'exit 0' TERM INT
trap cleanup EXIT

while [ "$(getprop sys.boot_completed)" != 1 ]; do
    [ -e "$ROOT/disabled" ] && exit 0
    sleep 2
done
while [ ! -e "$ROOT/disabled" ]; do
    if ! oscam_pid >/dev/null; then
        "$ROOT/oscam" -b -c "$ROOT/config" -t "$ROOT" -B "$ROOT/oscam.pid"
    fi
    if [ -z "$supervisor_pid" ] || ! kill -0 "$supervisor_pid" 2>/dev/null; then
        [ -n "$supervisor_pid" ] && wait "$supervisor_pid" 2>/dev/null
        # Keep startup diagnostics bounded; normal operation uses rotating bridge.log.
        [ -f "$ROOT/launcher.log" ] && [ "$(wc -c < "$ROOT/launcher.log")" -gt 131072 ] && : > "$ROOT/launcher.log"
        app_process /system/bin --nice-name=oscam-mtk-supervisor casprobe.BridgeService &
        supervisor_pid=$!
    fi
    sleep 3
done
