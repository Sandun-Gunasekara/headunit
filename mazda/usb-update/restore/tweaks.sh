#!/bin/sh
# Restores the ORIGINAL Android Auto headunit (the version that was installed before the first update).
# Runs from start to finish without any input (no touch screen or joystick needed).
# Uses headunit.orig / headunit-wrapper.orig saved on the car by the install USB, or, if those are
# missing, a backup_original/ folder copied onto this USB drive.

MYDIR=$(dirname $(readlink -f $0))
BIN_DIR=/tmp/mnt/data_persist/dev/bin
LOG="${MYDIR}/aa_restore_log.txt"
POPUP_SECONDS=${POPUP_SECONDS:-15}

log() { printf "%s\n" "$*" >> "$LOG"; /bin/fsync "$LOG" 2>/dev/null; }

# Status popups run in the background: nothing waits for a tap
popup() { # $1 info|error, $2 title, $3 text
    killall jci-dialog 2>/dev/null
    log "POPUP $1: $2: $3"
    if [ "$1" = error ]; then
        /jci/tools/jci-dialog --error --title="$2" --text="$3" --ok-label='OK' --no-cancel >/dev/null 2>&1 &
    else
        /jci/tools/jci-dialog --info --title="$2" --text="$3" --no-cancel >/dev/null 2>&1 &
    fi
}
abort() { # $1 text
    popup error "ANDROID AUTO RESTORE" "$1"
    log "ABORTED, nothing was changed"
    sleep $POPUP_SECONDS
    killall jci-dialog 2>/dev/null
    exit
}

log_storage() { # $1 label
    log "--- storage ($1)"
    df -k "$BIN_DIR" /data >> "$LOG" 2>&1
    log "AA files: $(du -sk "$BIN_DIR" 2>/dev/null | awk '{print $1}') KB, log: $(ls -l /data/headunit.log 2>/dev/null | awk '{print $5}') bytes"
}

same_file() {
    if command -v cmp >/dev/null 2>&1; then cmp -s "$1" "$2"; return $?; fi
    [ "$(md5sum < "$1")" = "$(md5sum < "$2")" ]
}

stop_headunit() {
    for pid in $(ps | grep '[h]eadunit-wrapper' | awk '{print $1}'); do kill -9 $pid 2>/dev/null; done
    killall -q -9 headunit
    sleep 1
}

log "===== Restore original: $(date)"
if [ ! -f "$LOG" ]; then
    abort "Cannot write to the USB drive. Nothing was changed."
fi
popup info "ANDROID AUTO RESTORE" "Restoring the original Android Auto. Do not remove the USB drive."
log "--- files before restore"; ls -la "$BIN_DIR" >> "$LOG" 2>&1
log_storage "before"

if [ -s "$BIN_DIR/headunit.orig" ] && [ -s "$BIN_DIR/headunit-wrapper.orig" ]; then
    SRC_BIN="$BIN_DIR/headunit.orig"
    SRC_WRAPPER="$BIN_DIR/headunit-wrapper.orig"
elif [ -s "${MYDIR}/backup_original/headunit" ] && [ -s "${MYDIR}/backup_original/headunit-wrapper" ]; then
    SRC_BIN="${MYDIR}/backup_original/headunit"
    SRC_WRAPPER="${MYDIR}/backup_original/headunit-wrapper"
else
    abort "No saved original found on the car or in backup_original/ on this USB drive. Nothing was changed. Remove the USB drive."
fi
log "Restoring from $SRC_BIN"

echo 1 > "/sys/class/gpio/Watchdog Disable/value" 2>/dev/null
stop_headunit

# Keep the current log (if debug logging is on) for later
cp /data/headunit.log "${MYDIR}/headunit_before_restore.log" 2>/dev/null

cp "$SRC_BIN" "$BIN_DIR/headunit.new" && same_file "$SRC_BIN" "$BIN_DIR/headunit.new" &&
    cp "$SRC_WRAPPER" "$BIN_DIR/headunit-wrapper.new" && same_file "$SRC_WRAPPER" "$BIN_DIR/headunit-wrapper.new"
if [ $? -ne 0 ]; then
    rm -f "$BIN_DIR/headunit.new" "$BIN_DIR/headunit-wrapper.new"
    popup error "ANDROID AUTO RESTORE" "Copying the original failed. Nothing was changed. Remove the USB drive; the system will reboot."
    sleep 10
    sync
    reboot
    exit
fi
chmod 755 "$BIN_DIR/headunit.new" "$BIN_DIR/headunit-wrapper.new"
mv -f "$BIN_DIR/headunit.new" "$BIN_DIR/headunit"
mv -f "$BIN_DIR/headunit-wrapper.new" "$BIN_DIR/headunit-wrapper"

log "--- files after restore"; ls -la "$BIN_DIR" >> "$LOG" 2>&1
log_storage "after"
log "Done"
popup info "ANDROID AUTO RESTORE" "Original version restored. Remove the USB drive; the system will reboot now."
sleep 10
sync
reboot
exit
