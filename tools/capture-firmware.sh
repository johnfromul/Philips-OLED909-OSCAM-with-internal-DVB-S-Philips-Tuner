#!/bin/sh
set -eu
[ "$#" = 1 ] || { echo 'usage: capture-firmware.sh TV_ADB_ENDPOINT' >&2; exit 2; }
ADB_BIN=${ADB:-adb}
"$ADB_BIN" -s "$1" shell 'for name in persist.odm.tpv.sys.ACR_ModName ro.odm.tpvision.product.swver ro.product.model ro.product.device ro.build.display.id ro.build.version.incremental ro.build.version.release ro.build.version.security_patch ro.build.fingerprint ro.vendor.build.fingerprint; do echo "$name=$(getprop "$name")"; done; uname -r'
"$ADB_BIN" -s "$1" shell "su -c 'id; su -v; magisk -v; getenforce'"
