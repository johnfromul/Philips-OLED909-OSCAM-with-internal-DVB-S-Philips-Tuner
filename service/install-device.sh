#!/system/bin/sh
# Fresh installation helper; see docs/install.md before running.
set -eu
umask 077
ROOT=/data/local/tmp/oscam-mtk
STAGE=${1:?Missing staging directory}
case "$STAGE" in /data/local/tmp/oscam-mtk-install-[0-9]*) :;; *) exit 2;; esac
[ "$(id -u)" = 0 ]
[ "$(getprop ro.odm.tpvision.product.swver)" = TPM231WW_R.201.100.146.220 ] || {
    echo 'Untested firmware: review the vendor ABI before installing.' >&2; exit 1;
}
[ -c /dev/dvb0.ca2 ] && [ -c /dev/dvb0.demux20 ]
[ -x /data/adb/magisk/busybox ]
[ ! -e "$ROOT" ] && [ ! -e /data/adb/service.d/oscam-mtk.sh ] || {
    echo 'An installation or partial installation exists. Inspect it or use the upgrade procedure.' >&2; exit 1;
}
for file in oscam.conf oscam.server oscam.user; do [ -f "$STAGE/config/$file" ]; done
for file in oscam-arm64 mtk-bridge bridge-service.jar control.sh watchdog.sh oscam-mtk.sh; do [ -f "$STAGE/$file" ]; done
mkdir "$ROOT"
chmod 700 "$ROOT"
mkdir "$ROOT/config"
chmod 700 "$ROOT/config"
cp "$STAGE/oscam-arm64" "$ROOT/oscam"
cp "$STAGE/mtk-bridge" "$ROOT/mtk-bridge"
cp "$STAGE/bridge-service.jar" "$ROOT/bridge-service.jar"
cp "$STAGE/control.sh" "$ROOT/control.sh"
cp "$STAGE/watchdog.sh" "$ROOT/watchdog.sh"
chmod 700 "$ROOT/oscam" "$ROOT/mtk-bridge" "$ROOT/control.sh" "$ROOT/watchdog.sh"
chmod 600 "$ROOT/bridge-service.jar"
for file in oscam.conf oscam.server oscam.user; do
    cp "$STAGE/config/$file" "$ROOT/config/$file"
    chmod 600 "$ROOT/config/$file"
    rm -f "$STAGE/config/$file"
done
rmdir "$STAGE/config"
mkdir -p /data/adb/service.d
cp "$STAGE/oscam-mtk.sh" /data/adb/service.d/oscam-mtk.sh
chmod 700 /data/adb/service.d/oscam-mtk.sh
"$ROOT/control.sh" start
echo 'Service launched. Verify WebIf, PID bindings and picture/sound using docs/install.md.'
