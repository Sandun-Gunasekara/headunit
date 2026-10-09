#!/bin/bash
# Builds the USB stick packages: one zip per package, with the files at the top level of the zip so
# they can be unzipped straight onto an empty FAT32 USB stick.
#   package.sh <stripped headunit binary> <version> <output dir>
# Normally run through "make usb-packages" in mazda/.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BINARY="$1"
VERSION="$2"
OUT="$3"
if [ ! -f "$BINARY" ] || [ -z "$VERSION" ] || [ -z "$OUT" ]; then
    echo "usage: $0 <headunit binary> <version> <output dir>"
    exit 1
fi
TRIGGER="$HERE/../installer"   # cmu_dataretrieval.up, dataRetrieval_config.txt, jci-autoupdate

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for pkg in install restore collect-logs cleanup hud-test; do
    dir="$WORK/$pkg"
    mkdir -p "$dir"
    sed "s/@VERSION@/$VERSION/g" "$HERE/$pkg/tweaks.sh" > "$dir/tweaks.sh"
    chmod 755 "$dir/tweaks.sh"
    cp "$TRIGGER/cmu_dataretrieval.up" "$TRIGGER/dataRetrieval_config.txt" "$TRIGGER/jci-autoupdate" "$dir/"
    if [ "$pkg" = install ] || [ "$pkg" = hud-test ]; then
        cp "$BINARY" "$dir/headunit"
        chmod 755 "$dir/headunit"
    fi
    zip_name="headunit-$VERSION-usb-$pkg.zip"
    rm -f "$OUT/$zip_name"
    (cd "$dir" && zip -q -X "$OUT/$zip_name" *)
    echo "$OUT/$zip_name"
done

cp "$BINARY" "$OUT/headunit-$VERSION"
(cd "$OUT" && sha256sum headunit-$VERSION headunit-$VERSION-usb-*.zip > "headunit-$VERSION-SHA256SUMS.txt")
echo "$OUT/headunit-$VERSION-SHA256SUMS.txt"
