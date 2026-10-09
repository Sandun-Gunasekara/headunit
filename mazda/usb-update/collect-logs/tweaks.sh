#!/bin/sh
# Copies the Android Auto logs to this USB drive. Changes nothing on the car and does not reboot.
# Runs from start to finish without any input (no touch screen or joystick needed).

MYDIR=$(dirname $(readlink -f $0))
BIN_DIR=/tmp/mnt/data_persist/dev/bin
OUT="${MYDIR}/aa_logs_$(date +%Y%m%d-%H%M%S)"
POPUP_SECONDS=${POPUP_SECONDS:-15}

popup() { # $1 text; shown in the background, nothing waits for a tap
    killall jci-dialog 2>/dev/null
    /jci/tools/jci-dialog --info --title="ANDROID AUTO LOGS" --text="$1" --no-cancel >/dev/null 2>&1 &
}

popup "Copying Android Auto logs. Do not remove the USB drive."
mkdir -p "$OUT"
grep '^JCI_SW_VER' /jci/version.ini > "$OUT/cmu_version.txt" 2>&1
ls -la "$BIN_DIR" > "$OUT/files.txt" 2>&1
{ df -k "$BIN_DIR" /data; echo; echo "AA files: $(du -sk "$BIN_DIR" 2>/dev/null | awk '{print $1}') KB"; } > "$OUT/storage.txt" 2>&1
ps > "$OUT/processes.txt" 2>&1
cp /data/headunit.log "$OUT/" 2>/dev/null
cp /tmp/mnt/data/headunit.log "$OUT/headunit_mnt_data.log" 2>/dev/null
cp /tmp/root/headunit.json "$OUT/" 2>/dev/null
dmesg > "$OUT/dmesg.txt" 2>&1
/bin/fsync "$OUT/files.txt" 2>/dev/null
sync

popup "Logs copied to the USB drive. You can remove it now."
sleep $POPUP_SECONDS
killall jci-dialog 2>/dev/null
exit
