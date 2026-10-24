#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}
JAVAC_BIN=${JAVAC:-javac}
ANDROID_JAR="$SDK/platforms/android-34/android.jar"
D8="$SDK/build-tools/36.1.0/d8"
[ -f "$ANDROID_JAR" ] && [ -x "$D8" ] || {
    echo 'Android SDK platform 34 and build-tools 36.1.0 are required; set ANDROID_SDK_ROOT.' >&2
    exit 1
}
mkdir -p "$ROOT/build/java"
sh "$ROOT/tools/check.sh"
"$ROOT/tools/arm64-cc" -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/src/compat" \
    "$ROOT/src/protocol.c" "$ROOT/src/mtk_bridge.c" -o "$ROOT/build/mtk-bridge"
"$JAVAC_BIN" --release 17 -cp "$ANDROID_JAR" -d "$ROOT/build/java" "$ROOT/src/casprobe/BridgeService.java"
"$D8" --min-api 34 --lib "$ANDROID_JAR" --output "$ROOT/build/bridge-service.jar" \
    "$ROOT/build/java/casprobe/BridgeService.class" "$ROOT/build/java/casprobe/BridgeService\$Running.class"
echo 'Built build/mtk-bridge and build/bridge-service.jar'
