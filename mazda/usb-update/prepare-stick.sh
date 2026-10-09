#!/bin/bash
# Copies one of the car-usb packages onto a FAT32 USB stick without the hidden files macOS adds
# (._* AppleDouble files: "._cmu_dataretrieval.up" also ends in .up and stops the CMU from running
# the real one), then ejects the stick.
#   ./prepare-stick.sh 2-restore-original /Volumes/MYSTICK
set -e
PKG="$(cd "$(dirname "$0")" && pwd)/${1%/}"
VOL="${2%/}"
if [ ! -f "$PKG/tweaks.sh" ] || [ ! -d "$VOL" ] || [ "$VOL" = "/" ]; then
    echo "usage: $0 <1-install-test-build|2-restore-original|3-collect-logs|4-finish-cleanup> /Volumes/<stick>"
    exit 1
fi
mdutil -i off "$VOL" >/dev/null 2>&1 || true
# COPYFILE_DISABLE stops cp writing ._ files
COPYFILE_DISABLE=1 cp -X "$PKG"/* "$VOL"/
dot_clean -m "$VOL" 2>/dev/null || true
find "$VOL" -maxdepth 1 -name '._*' -delete
sync
echo "On the stick:"
ls -la "$VOL"
if ls -a "$VOL" | grep -q '^\._'; then echo "WARNING: ._ files are still on the stick"; exit 1; fi
diskutil eject "$VOL"
