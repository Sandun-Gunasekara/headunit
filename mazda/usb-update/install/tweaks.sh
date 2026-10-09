#!/bin/sh
# Installs Android Auto headunit @VERSION@ on the Mazda CMU, over an existing Android Auto install
# (e.g. from MZD-AIO tweaks). Runs from start to finish without any input (no touch screen or
# joystick needed).
#
# Only the headunit binary is replaced. The version installed before is kept on the car as
# headunit.orig / headunit-wrapper.orig (only the first time, so it stays the original) and copied
# to this USB drive, so the "restore" USB can put it back. If the new binary does not start, the
# previous version is restored automatically.

MYDIR=$(dirname $(readlink -f $0))
BIN_DIR=/tmp/mnt/data_persist/dev/bin
LOG="${MYDIR}/aa_install_log.txt"
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
# Stop without changing anything: show why for a while, then close the popup
abort() { # $1 text
    popup error "ANDROID AUTO UPDATE" "$1"
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

same_file() { # $1 $2: identical contents?
    if command -v cmp >/dev/null 2>&1; then cmp -s "$1" "$2"; return $?; fi
    [ "$(md5sum < "$1")" = "$(md5sum < "$2")" ]
}

stop_headunit() { # stop the wrapper first so it does not restart headunit while we work
    for pid in $(ps | grep '[h]eadunit-wrapper' | awk '{print $1}'); do kill -9 $pid 2>/dev/null; done
    killall -q -9 headunit
    sleep 1
}

test_binary() { # $1: binary. Runs its built-in self test with the same environment as headunit-wrapper.
    ( SCRIPTPATH="$BIN_DIR"; eval "$(grep '^export ' "$BIN_DIR/headunit-wrapper")"; "$1" test ) > "${MYDIR}/selftest.txt" 2>&1
    cat "${MYDIR}/selftest.txt" >> "$LOG"
    grep -q "###TESTMODE_OK###" "${MYDIR}/selftest.txt"
}

restore_original() {
    log "Restoring original files"
    cp -a "$BIN_DIR/headunit.orig" "$BIN_DIR/headunit.new" && mv -f "$BIN_DIR/headunit.new" "$BIN_DIR/headunit"
    cp -a "$BIN_DIR/headunit-wrapper.orig" "$BIN_DIR/headunit-wrapper"
    chmod 755 "$BIN_DIR/headunit" "$BIN_DIR/headunit-wrapper"
}

# ---------------------------------------------------------------- checks (nothing is changed yet)
log "===== Install Android Auto @VERSION@: $(date)"
if [ ! -f "$LOG" ]; then
    abort "Cannot write to the USB drive. Nothing was changed."
fi
popup info "ANDROID AUTO UPDATE" "Installing Android Auto @VERSION@. Do not remove the USB drive."
log "CMU version: $(grep '^JCI_SW_VER=' /jci/version.ini)"
log "--- current files"; ls -la "$BIN_DIR" >> "$LOG" 2>&1
log_storage "before"

if [ ! -f "$BIN_DIR/headunit" ] || [ ! -f "$BIN_DIR/headunit-wrapper" ]; then
    abort "Android Auto was not found in $BIN_DIR. Nothing was changed. Remove the USB drive."
fi
if [ ! -f "${MYDIR}/headunit" ]; then
    abort "The file 'headunit' is missing from the USB drive. Nothing was changed. Remove the USB drive."
fi

# Space for headunit.orig + headunit.new (a copy of the new version) + 1 MB spare
FREE_KB=$(df -k "$BIN_DIR" | awk 'NR==2 {print $4}')
ORIG_KB=$(( $(ls -l "$BIN_DIR/headunit" | awk '{print $5}') / 1024 ))
NEW_KB=$(( $(ls -l "${MYDIR}/headunit" | awk '{print $5}') / 1024 ))
if [ -f "$BIN_DIR/headunit.orig" ]; then ORIG_KB=0; fi
NEED_KB=$(( ORIG_KB + NEW_KB + 1024 ))
log "Storage check: ${FREE_KB} KB free, ${NEED_KB} KB needed (backup ${ORIG_KB} KB + new version ${NEW_KB} KB + 1024 KB spare)"
if [ -z "$FREE_KB" ]; then
    abort "Could not read the free space. Nothing was changed. Remove the USB drive."
fi
if [ "$FREE_KB" -lt "$NEED_KB" ]; then
    abort "Not enough free space (${FREE_KB} KB free, ${NEED_KB} KB needed). Nothing was changed. Remove the USB drive."
fi

# Same as the official installer: stop the watchdog rebooting the CMU while we work
echo 1 > "/sys/class/gpio/Watchdog Disable/value" 2>/dev/null

stop_headunit
log "--- installed headunit self test (shows its version)"
test_binary "$BIN_DIR/headunit"

# ---------------------------------------------------------------- back up the originals
# Never overwrite an existing .orig: after a second test install it must still be the original.
if [ ! -f "$BIN_DIR/headunit.orig" ]; then
    cp -a "$BIN_DIR/headunit" "$BIN_DIR/headunit.orig"
    cp -a "$BIN_DIR/headunit-wrapper" "$BIN_DIR/headunit-wrapper.orig"
    if ! same_file "$BIN_DIR/headunit" "$BIN_DIR/headunit.orig" || ! same_file "$BIN_DIR/headunit-wrapper" "$BIN_DIR/headunit-wrapper.orig"; then
        rm -f "$BIN_DIR/headunit.orig" "$BIN_DIR/headunit-wrapper.orig"
        popup error "ANDROID AUTO UPDATE" "Backing up the installed version failed. Nothing was changed. Remove the USB drive; the system will reboot."
        sleep 10
        sync
        reboot
        exit
    fi
    log "Saved headunit.orig and headunit-wrapper.orig on the car"
else
    log "headunit.orig already exists (from an earlier install), keeping it as the original"
fi
if [ ! -s "$BIN_DIR/headunit.orig" ] || [ ! -s "$BIN_DIR/headunit-wrapper.orig" ]; then
    popup error "ANDROID AUTO UPDATE" "The saved original version is missing or empty. Nothing was changed. Remove the USB drive; the system will reboot."
    sleep 10
    sync
    reboot
    exit
fi
if [ ! -d "${MYDIR}/backup_original" ]; then
    mkdir -p "${MYDIR}/backup_original"
    cp -a "$BIN_DIR/headunit.orig" "${MYDIR}/backup_original/headunit"
    cp -a "$BIN_DIR/headunit-wrapper.orig" "${MYDIR}/backup_original/headunit-wrapper"
    cp -a "$BIN_DIR/headunit.json" "${MYDIR}/backup_original/" 2>/dev/null
    sync
    log "Copied the originals to the USB drive (backup_original/)"
fi

# ---------------------------------------------------------------- install
cp "${MYDIR}/headunit" "$BIN_DIR/headunit.new" && same_file "${MYDIR}/headunit" "$BIN_DIR/headunit.new"
if [ $? -ne 0 ]; then
    rm -f "$BIN_DIR/headunit.new"
    popup error "ANDROID AUTO UPDATE" "Copying the new version failed. The installed version is unchanged. Remove the USB drive; the system will reboot."
    sleep 10
    sync
    reboot
    exit
fi
chmod 755 "$BIN_DIR/headunit.new"
mv -f "$BIN_DIR/headunit.new" "$BIN_DIR/headunit"
log "Installed Android Auto @VERSION@"

log "--- new version self test"
if ! test_binary "$BIN_DIR/headunit"; then
    restore_original
    log_storage "after automatic restore"
    popup error "ANDROID AUTO UPDATE" "Android Auto @VERSION@ did not start, so the previous version was restored. Remove the USB drive; the system will reboot."
    sleep 10
    sync
    reboot
    exit
fi

log "--- files after install"; ls -la "$BIN_DIR" >> "$LOG" 2>&1
log_storage "after"
log "Done"
popup info "ANDROID AUTO UPDATE" "Android Auto @VERSION@ installed. Remove the USB drive; the system will reboot now."
sleep 10
sync
reboot
exit
