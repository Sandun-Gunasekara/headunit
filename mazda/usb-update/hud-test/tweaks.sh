#!/bin/sh
# HUD test: shows a numbered sequence of arrows and distances on the HUD so we can see which ones the
# HUD displays (why distances over 1 km disappear, and whether the HUD needs regular refreshing). Also records the USB hub details and the
# HUD/navigation dbus methods this firmware offers.
# Runs from start to finish without any input. Uses the headunit binary on this USB drive, so the
# installed Android Auto is not changed. Reboots at the end to bring Android Auto back.
# Park the car (engine on, so the HUD is on) and film the HUD with a phone for the whole test (~5 min).

MYDIR=$(dirname $(readlink -f $0))
BIN_DIR=/tmp/mnt/data_persist/dev/bin
LOG="${MYDIR}/aa_hud_test_log.txt"
STEP_SECONDS=${STEP_SECONDS:-8}
ICON_SECONDS=${ICON_SECONDS:-3}

log() { printf "%s\n" "$*" >> "$LOG"; /bin/fsync "$LOG" 2>/dev/null; }
popup() { # $1 text, shown in the background: nothing waits for a tap
    killall jci-dialog 2>/dev/null
    log "POPUP: $1"
    /jci/tools/jci-dialog --info --title="HUD TEST" --text="$1" --no-cancel >/dev/null 2>&1 &
}

stop_headunit() { # so the running Android Auto doesn't overwrite the HUD during the test
    for pid in $(ps | grep '[h]eadunit-wrapper' | awk '{print $1}'); do kill -9 $pid 2>/dev/null; done
    killall -q -9 headunit
    sleep 1
}

run_headunit() { # same environment as headunit-wrapper
    ( SCRIPTPATH="$BIN_DIR"; eval "$(grep '^export ' "$BIN_DIR/headunit-wrapper")"; "${MYDIR}/headunit" "$@" ) >> "$LOG" 2>&1
}

step() { # $1 label, $2 description, $3 icon, $4 distance, $5 unit, $6 seconds
    popup "$1: $2. Watch the HUD."
    log "=== $1: $2 (icon $3, distance $4, unit $5, $6 s)"
    # Keeps the connection open and resends every second, like a running Android Auto session
    run_headunit hudtest "$3" "$4" "$5" 1 "$1" "$6" 1
}

log "===== HUD test: $(date)"
log "CMU version: $(grep '^JCI_SW_VER=' /jci/version.ini)"
if [ ! -f "${MYDIR}/headunit" ] || [ ! -f "$BIN_DIR/headunit-wrapper" ]; then
    popup "The HUD test can't run (files missing). Nothing was changed. Remove the USB drive."
    log "ABORTED: files missing"
    sleep 15
    killall jci-dialog 2>/dev/null
    exit
fi

# USB details for the adaptor reconnect problem (read only)
log "--- usb devices (sysfs)"
for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] || continue
    log "$(basename $d): $(cat $d/idVendor):$(cat $d/idProduct) bus $(cat $d/busnum 2>/dev/null) dev $(cat $d/devnum 2>/dev/null) maxchild $(cat $d/maxchild 2>/dev/null) $(cat $d/product 2>/dev/null)"
done
log "--- usbinfo"
run_headunit usbinfo
# What the HUD/navigation services on this firmware offer, e.g. another way to show street names (read only)
log "--- dbusinfo"
run_headunit dbusinfo

popup "HUD test starting in 10 seconds. Start filming the HUD now (about 5 minutes)."
sleep 10
stop_headunit

# Part A: where does the HUD stop showing guidance? Right arrow (shows fine at short distances)
#    label         description                                  icon distance unit seconds
step "A1 of 11"  "right arrow 100 m (control, should show)"    3    1000     1    $STEP_SECONDS
n=2
for m in 300 320 350 400 450 500 600 800 999; do
    step "A$n of 11" "right arrow $m m" 3 $((m * 10)) 1 $STEP_SECONDS
    n=$((n + 1))
done
step "A11 of 11" "right arrow 100 m again (control)"           3    1000     1    $STEP_SECONDS

# Part B: can the HUD show an arrow without a distance (for turns that are far away)?
step "B1 of 4"   "right arrow, distance 0 m"                   3    0        1    $STEP_SECONDS
step "B2 of 4"   "right arrow, distance 0, no unit"            3    0        0    $STEP_SECONDS
step "B3 of 4"   "right arrow, 1.5 km, no unit"                3    15       0    $STEP_SECONDS
step "B4 of 4"   "right arrow 100 m (control)"                 3    1000     1    $STEP_SECONDS

# Part C: icon codes not seen yet, all at a short distance; the distance shows the code (code 37 -> 37 m)
for code in 24 25 26 27 28 29 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60; do
    step "C code $code" "icon code $code, shown as $code m" $code $((code * 10)) 1 $ICON_SECONDS
done

step "End" "clear the HUD" 0 0 1 3

log "Done"
popup "HUD test finished. Remove the USB drive; the system will reboot now."
sleep 10
sync
reboot
exit
