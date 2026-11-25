#!/system/bin/sh
set -u
umask 077
BB=/data/adb/magisk/busybox
ROOT=/data/local/tmp/oscam-mtk
USB=/mnt/media_rw/092C-1A07
FALLBACK=/data/media/0/Download/oscam-diagnostics
export TZ='CET-1CEST,M3.5.0,M10.5.0/3'
mkdir -p "$ROOT"
if [ "${1:-}" != --locked ]; then
    exec "$BB" flock -n "$ROOT/diagnostics-collector.lock" /system/bin/sh "$0" --locked
fi
tmp="$ROOT/diagnostics-collector.$$.chunk"
trap 'rm -f "$tmp"; exit 0' TERM INT
b_inode=0; b_offset=0; o_inode=0; o_offset=0; last_target=

choose_target() {
    if grep -q " $USB " /proc/mounts && mkdir -p "$USB/oscam-diagnostics" 2>/dev/null && [ -w "$USB/oscam-diagnostics" ]; then
        target="$USB/oscam-diagnostics"
    else
        mkdir -p "$FALLBACK" || return 1
        target="$FALLBACK"
    fi
}
rotate() {
    rotate_file="$1"
    rotate_size=$("$BB" stat -c %s "$rotate_file" 2>/dev/null) || rotate_size=0
    if [ "$rotate_size" -ge 5242880 ]; then
        rm -f "$rotate_file.5"
        for n in 4 3 2 1; do [ ! -f "$rotate_file.$n" ] || mv -f "$rotate_file.$n" "$rotate_file.$((n+1))"; done
        mv -f "$rotate_file" "$rotate_file.1"
    fi
}
collect() {
    source_file="$1"; out_name="$2"; old_inode="$3"; old_offset="$4"
    info=$("$BB" stat -c '%i %s' "$source_file" 2>/dev/null) || return 0
    set -- $info; inode="$1"; size="$2"
    [ "$inode" = "$old_inode" ] && [ "$size" -ge "$old_offset" ] || old_offset=0
    [ "$size" -gt "$old_offset" ] || { new_inode="$inode"; new_offset="$size"; return 0; }
    "$BB" tail -c "+$((old_offset+1))" "$source_file" > "$tmp" || return 1
    rotate "$target/$out_name"
    if [ "$out_name" = oscam.log ]; then
        grep -E '\((ecm|dvbapi)\)' "$tmp" >> "$target/$out_name" || [ "$?" -eq 1 ] || return 1
    else
        cat "$tmp" >> "$target/$out_name" || return 1
    fi
    new_inode="$inode"; new_offset="$size"
}
while :; do
    if choose_target; then
        if [ "$target" != "$last_target" ]; then
            printf '%s collector destination=%s\n' "$(date '+%F %T')" "$target" >> "$target/collector.log" 2>/dev/null || true
            last_target="$target"
        fi
        new_inode="$b_inode"; new_offset="$b_offset"
        if ! collect "$ROOT/bridge.log" bridge.log "$b_inode" "$b_offset"; then
            mkdir -p "$FALLBACK" && target="$FALLBACK" && last_target= && collect "$ROOT/bridge.log" bridge.log "$b_inode" "$b_offset" || true
        fi
        b_inode="$new_inode"; b_offset="$new_offset"
        new_inode="$o_inode"; new_offset="$o_offset"
        if ! collect "$ROOT/oscam.log" oscam.log "$o_inode" "$o_offset"; then
            mkdir -p "$FALLBACK" && target="$FALLBACK" && last_target= && collect "$ROOT/oscam.log" oscam.log "$o_inode" "$o_offset" || true
        fi
        o_inode="$new_inode"; o_offset="$new_offset"
    fi
    sleep 3
done
