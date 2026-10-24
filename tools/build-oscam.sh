#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/upstream/oscam"
EXPECTED=39e78a4688b30abaab62b5c9e13672b4e34bfca0
mkdir -p "$ROOT/upstream" "$ROOT/build"
if [ ! -d "$SOURCE/.git" ]; then
    git clone --depth 1 --branch 11968 https://git.streamboard.tv/common/oscam.git "$SOURCE"
fi
[ "$(git -C "$SOURCE" rev-parse HEAD)" = "$EXPECTED" ] || {
    echo 'Unexpected upstream commit; refusing to patch a different revision.' >&2; exit 1;
}
PATCH="$ROOT/patches/oscam-11968-android-dvbapi-v1.patch"
if git -C "$SOURCE" apply --check "$PATCH" 2>/dev/null; then
    git -C "$SOURCE" apply "$PATCH"
else
    git -C "$SOURCE" apply --reverse --check "$PATCH"
fi
cd "$SOURCE"
./config.sh --disable all
./config.sh --enable WEBIF WEBIF_LIVELOG WEBIF_JQUERY WITH_COMPRESS_WEBIF HAVE_DVBAPI \
    WITH_EXTENDED_CW MODULE_CCCAM MODULE_CCCSHARE WITH_DEBUG
make -j"${JOBS:-4}" uname_S=Linux CC="$ROOT/tools/arm64-cc" HOSTCC=clang STRIP=true \
    CONF_DIR=/data/local/tmp/oscam-mtk/config OSCAM_BIN="$ROOT/build/oscam-arm64" \
    EXTRA_LDFLAGS=-static EXTRA_CC_WARN=-Wno-error=date-time
git rev-parse HEAD > "$ROOT/build/oscam-upstream-commit.txt"
echo 'Built build/oscam-arm64'
