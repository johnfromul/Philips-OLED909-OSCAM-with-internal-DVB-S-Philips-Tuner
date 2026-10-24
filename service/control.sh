#!/system/bin/sh
set -u
umask 077
ROOT=/data/local/tmp/oscam-mtk
BB=/data/adb/magisk/busybox
case "${1:-status}" in
    start)
        rm -f "$ROOT/disabled"
        "$BB" nohup "$BB" flock -n "$ROOT/launcher.lock" /system/bin/sh "$ROOT/watchdog.sh" \
            </dev/null >>"$ROOT/launcher.log" 2>&1 &
        ;;
    stop)
        : > "$ROOT/disabled"
        count=0
        while [ -e "$ROOT/watchdog.pid" ] && [ "$count" -lt 12 ]; do
            sleep 1
            count=$((count+1))
        done
        [ ! -e "$ROOT/watchdog.pid" ] || { echo 'STOP_PENDING: inspect launcher.log'; exit 1; }
        # cleanup removes its PID just before the flock parent releases its lock.
        # Wait for that release so an immediately following start cannot be lost.
        count=0
        until "$BB" flock -n "$ROOT/launcher.lock" "$BB" true; do
            [ "$count" -lt 5 ] || { echo 'STOP_PENDING: launcher lock still held'; exit 1; }
            sleep 1
            count=$((count+1))
        done
        ;;
    status)
        [ -e "$ROOT/disabled" ] && echo DISABLED || echo ENABLED
        for name in watchdog supervisor oscam; do
            process=$(cat "$ROOT/$name.pid" 2>/dev/null)
            case "$process" in ''|*[!0-9]*) echo "$name: stopped";;
                *) if kill -0 "$process" 2>/dev/null; then echo "$name: pid=$process"; else echo "$name: stopped"; fi;;
            esac
        done
        ;;
    *) echo 'usage: control.sh start|stop|status'; exit 2;;
esac
