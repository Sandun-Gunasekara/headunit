#!/bin/sh
# Keep the Android Auto version that is installed now and remove what the update left on the car:
# headunit.orig / headunit-wrapper.orig (the previous version, copied to this USB drive first) and
# the debug log. Debug logging is switched off.
# Runs from start to finish without any input (no touch screen or joystick needed).

MYDIR=$(dirname $(readlink -f $0))
BIN_DIR=/tmp/mnt/data_persist/dev/bin
LOG="${MYDIR}/aa_cleanup_log.txt"
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
    popup error "ANDROID AUTO CLEANUP" "$1"
    log "ABORTED, nothing was deleted"
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

log "===== Cleanup: $(date)"
if [ ! -f "$LOG" ]; then
    abort "Cannot write to the USB drive. Nothing was deleted."
fi
popup info "ANDROID AUTO CLEANUP" "Removing the backup of the previous Android Auto version and the debug log. Do not remove the USB drive."
log "--- files before cleanup"; ls -la "$BIN_DIR" >> "$LOG" 2>&1
log_storage "before"

# 1. Save the original off the car before deleting it
if [ -f "$BIN_DIR/headunit.orig" ] || [ -f "$BIN_DIR/headunit-wrapper.orig" ]; then
    mkdir -p "${MYDIR}/backup_original"
    for f in headunit headunit-wrapper; do
        [ -f "$BIN_DIR/$f.orig" ] || continue
        cp "$BIN_DIR/$f.orig" "${MYDIR}/backup_original/$f"
        if ! same_file "$BIN_DIR/$f.orig" "${MYDIR}/backup_original/$f"; then
            abort "Could not copy the original to the USB drive, so nothing was deleted. Try another USB drive."
        fi
    done
    /bin/fsync "${MYDIR}/backup_original/headunit" 2>/dev/null
    sync
    rm -f "$BIN_DIR/headunit.orig" "$BIN_DIR/headunit-wrapper.orig"
    log "Original saved to backup_original/ on the USB drive and removed from the car"
else
    log "No .orig files on the car"
fi

# 2. Logging off, save and remove the log
sed -i 's/^DEBUG=1/DEBUG=0/' "$BIN_DIR/headunit-wrapper"
cp /data/headunit.log "${MYDIR}/headunit_final.log" 2>/dev/null
rm -f /data/headunit.log
log "Debug logging off, log saved as headunit_final.log and removed from the car"

log "--- files after cleanup"; ls -la "$BIN_DIR" >> "$LOG" 2>&1
log_storage "after"
log "Done"
popup info "ANDROID AUTO CLEANUP" "Cleanup done. Keep this USB drive: it has a copy of your previous Android Auto version. Remove it; the system will reboot now."
sleep 10
sync
reboot
exit
