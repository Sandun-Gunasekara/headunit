#!/bin/bash
# Tests the USB stick packages against a fake CMU file layout using busybox (the CMU's shell and
# tools). Builds the zips with package.sh (fake headunit binary), unzips them and runs each script:
# install, re-install, restore, automatic restore of a broken build, missing AA, restore from the
# stick's backup, collect logs, cleanup, HUD test. Popups never get tapped (they block), so every
# script must finish by itself.
#   docker run --rm -v "$PWD/headunit:/src:ro" headunit-dev bash /src/mazda/usb-update/test-usb-scripts.sh
set -u
apt-get update -qq >/dev/null 2>&1; apt-get install -y -qq busybox zip unzip >/dev/null 2>&1
mkdir -p /bb && busybox --install -s /bb

# Build the packages like a release, with a fake binary, and unzip them to /u/<package>
HERE="$(cd "$(dirname "$0")" && pwd)"
printf '#!/bin/sh\necho "headunit version: TESTBUILD"\n[ "$1" = test ] && echo "###TESTMODE_OK###"\nexit 0\n' > /tmp/fake_headunit
rm -rf /tmp/dist /u && mkdir -p /u
bash "$HERE/package.sh" /tmp/fake_headunit 9.9.9 /tmp/dist >/dev/null
for pkg in install restore collect-logs cleanup hud-test; do
    mkdir -p /u/$pkg && (cd /u/$pkg && unzip -q /tmp/dist/headunit-9.9.9-usb-$pkg.zip)
done

BIN=/tmp/mnt/data_persist/dev/bin
PASS=0; FAIL=0
check() { if eval "$2"; then echo "  ok   $1"; PASS=$((PASS+1)); else echo "  FAIL $1"; FAIL=$((FAIL+1)); fi; }

fake_binary() { # $1 path, $2 version, $3 ok|bad
    if [ "$3" = ok ]; then
        printf '#!/bin/sh\necho "headunit version: %s"\n[ "$1" = test ] && echo "###TESTMODE_OK###"\nexit 0\n' "$2" > "$1"
    else
        printf '#!/bin/sh\necho "error while loading shared libraries"\nexit 127\n' > "$1"
    fi
    chmod 755 "$1"
}

reset_car() { # fresh fake CMU with the ORIGINAL AA install
    rm -rf /tmp/mnt /data /jci /tmp/rebooted /tmp/dialog.log /usb
    mkdir -p $BIN/headunit_libs /tmp/mnt/data /jci/tools /tmp/root
    ln -s /tmp/mnt/data /data
    echo 'JCI_SW_VER="ID_59.00.443_NA"' > /jci/version.ini
    fake_binary $BIN/headunit ORIGINAL ok
    printf '#!/bin/sh\nDEBUG=0\nexport LD_LIBRARY_PATH="${SCRIPTPATH}/headunit_libs:/jci/lib"\nexport GST_PLUGIN_PATH="${SCRIPTPATH}/headunit_libs"\n' > $BIN/headunit-wrapper
    chmod 755 $BIN/headunit-wrapper
    echo '{"wifiTransport": false}' > $BIN/headunit.json
    echo "original log" > /data/headunit.log
    # jci-dialog: like a dialog waiting for a tap that never comes (no touch screen/joystick)
    printf '#!/bin/sh\necho "$@" >> /tmp/dialog.log\nsleep 300\n' > /jci/tools/jci-dialog
    chmod 755 /jci/tools/jci-dialog
    cp $BIN/headunit /tmp/original_headunit; cp $BIN/headunit-wrapper /tmp/original_wrapper
}

usb() { # $1 folder -> fresh copy at /usb/$1 with a fake headunit build ($2 ok|bad)
    rm -rf /usb/$1; mkdir -p /usb; cp -a /u/$1 /usb/$1
    [ $# -ge 2 ] && fake_binary /usb/$1/headunit TESTBUILD $2
    return 0
}

run() { # $1 script; busybox sh with only busybox applets, fake reboot/fsync first in PATH
    mkdir -p /fakebin
    printf '#!/bin/sh\ntouch /tmp/rebooted\n' > /fakebin/reboot; chmod 755 /fakebin/reboot
    ln -sf /bb/true /bin/fsync 2>/dev/null
    # Must finish on its own: fail if anything waits for input
    POPUP_SECONDS=0 PATH=/fakebin:/bb /usr/bin/timeout 60 /bin/busybox sh "$1" > /tmp/run_output.txt 2>&1
    if [ $? -eq 124 ]; then echo "  FAIL $1 did not finish by itself (waiting for input?)"; FAIL=$((FAIL+1)); fi
    pkill -f "sleep 300" 2>/dev/null
}

same() { cmp -s "$1" "$2"; }

echo "A. install test build"
reset_car; usb install ok; run /usb/install/tweaks.sh
check "headunit is the test build"        'same $BIN/headunit /usb/install/headunit'
check "headunit.orig is the original"     'same $BIN/headunit.orig /tmp/original_headunit'
check "wrapper.orig is the original"      'same $BIN/headunit-wrapper.orig /tmp/original_wrapper'
check "debug logging not changed"         'same $BIN/headunit-wrapper /tmp/original_wrapper'
check "version stamped in popups"         'grep -q "Android Auto 9.9.9 installed" /tmp/dialog.log'
check "originals copied to USB"           'same /usb/install/backup_original/headunit /tmp/original_headunit'
check "rebooted"                          '[ -f /tmp/rebooted ]'
check "no question popups"                '! grep -q -- "--question" /tmp/dialog.log'
check "storage check logged"              'grep -q "Storage check: .* KB free, .* KB needed" /usb/install/aa_install_log.txt && grep -q "storage (after)" /usb/install/aa_install_log.txt'
check "log shows both versions"           'grep -q "version: ORIGINAL" /usb/install/aa_install_log.txt && grep -q "version: TESTBUILD" /usb/install/aa_install_log.txt'

echo "B. install again: .orig must stay the original"
rm -f /tmp/rebooted; usb install ok; run /usb/install/tweaks.sh
check "headunit.orig still the original"  'same $BIN/headunit.orig /tmp/original_headunit'
check "wrapper.orig still the original"   'same $BIN/headunit-wrapper.orig /tmp/original_wrapper'

echo "C. restore original"
rm -f /tmp/rebooted; usb restore; run /usb/restore/tweaks.sh
check "headunit is the original"          'same $BIN/headunit /tmp/original_headunit'
check "wrapper is the original"           'same $BIN/headunit-wrapper /tmp/original_wrapper'
check "restore storage logged"            'grep -q "storage (after)" /usb/restore/aa_restore_log.txt'
check "previous log saved to USB"         '[ -f /usb/restore/headunit_before_restore.log ]'
check "rebooted"                          '[ -f /tmp/rebooted ]'

echo "D. test build that does not start -> original restored automatically"
reset_car; usb install bad; run /usb/install/tweaks.sh
check "headunit is the original"          'same $BIN/headunit /tmp/original_headunit'
check "wrapper is the original"           'same $BIN/headunit-wrapper /tmp/original_wrapper'
check "error popup shown"                 'grep -q "did not start" /tmp/dialog.log'

echo "F. no AA installed -> nothing changed"
reset_car; rm -f $BIN/headunit; usb install ok; run /usb/install/tweaks.sh
check "no headunit created"               '[ ! -f $BIN/headunit ]'
check "error popup shown"                 'grep -q "was not found" /tmp/dialog.log'

echo "G. restore when .orig is gone from the car but backup_original/ is on the USB"
reset_car; usb install ok; run /usb/install/tweaks.sh
rm -f $BIN/headunit.orig $BIN/headunit-wrapper.orig
usb restore; cp -a /usb/install/backup_original /usb/restore/
run /usb/restore/tweaks.sh
check "headunit is the original"          'same $BIN/headunit /tmp/original_headunit'

echo "H. collect logs changes nothing"
reset_car; usb collect-logs; run /usb/collect-logs/tweaks.sh
check "storage copied"                    '[ -s /usb/collect-logs/aa_logs_*/storage.txt ]'
check "log copied"                        'ls /usb/collect-logs/aa_logs_*/headunit.log >/dev/null 2>&1'
check "headunit unchanged"                'same $BIN/headunit /tmp/original_headunit'
check "no reboot"                         '[ ! -f /tmp/rebooted ]'

echo "I. cleanup after a test install keeps the new build, removes backups and log"
reset_car; usb install ok; run /usb/install/tweaks.sh
echo "test drive log" > /data/headunit.log
rm -f /tmp/rebooted; usb cleanup; run /usb/cleanup/tweaks.sh
check "headunit is still the test build"  'same $BIN/headunit /usb/install/headunit'
check ".orig files removed from car"      '[ ! -f $BIN/headunit.orig ] && [ ! -f $BIN/headunit-wrapper.orig ]'
check "original copied to cleanup USB"    'same /usb/cleanup/backup_original/headunit /tmp/original_headunit && same /usb/cleanup/backup_original/headunit-wrapper /tmp/original_wrapper'
check "debug logging off"                 'grep -q "^DEBUG=0" $BIN/headunit-wrapper'
check "cleanup storage logged"            'grep -q "storage (after)" /usb/cleanup/aa_cleanup_log.txt'
check "log saved to USB, removed from car" '[ -f /usb/cleanup/headunit_final.log ] && [ ! -f /data/headunit.log ]'
check "rebooted"                          '[ -f /tmp/rebooted ]'

echo "J. after cleanup, restore works from the cleanup USB's copy"
usb restore; cp -a /usb/cleanup/backup_original /usb/restore/
run /usb/restore/tweaks.sh
check "headunit is the original"          'same $BIN/headunit /tmp/original_headunit'
check "wrapper is the original"           'same $BIN/headunit-wrapper /tmp/original_wrapper'

echo "L. HUD test stick runs all steps, changes nothing, reboots"
reset_car; usb hud-test
printf '#!/bin/sh\necho "fake headunit $*" >> /tmp/hudtest_calls.txt\n[ "$1" = usbinfo ] && echo "device bus 1 address 1: 1d6b:0002 class 9"\n[ "$1" = hudtest ] && echo "HUDTEST: SENT"\nexit 0\n' > /usb/hud-test/headunit
chmod 755 /usb/hud-test/headunit; rm -f /tmp/hudtest_calls.txt
STEP_SECONDS=0 ICON_SECONDS=0 run /usb/hud-test/tweaks.sh
check "52 hud steps sent"                 '[ "$(grep -c "^fake headunit hudtest" /tmp/hudtest_calls.txt)" = 52 ]'
check "A1 right arrow 100 m control"      'grep -q "^fake headunit hudtest 3 1000 1 1 A1 of 11 0 1$" /tmp/hudtest_calls.txt'
check "A2 right arrow 300 m"              'grep -q "^fake headunit hudtest 3 3000 1 1 A2 of 11 0 1$" /tmp/hudtest_calls.txt'
check "A10 right arrow 999 m"             'grep -q "^fake headunit hudtest 3 9990 1 1 A10 of 11 0 1$" /tmp/hudtest_calls.txt'
check "B2 no distance no unit"            'grep -q "^fake headunit hudtest 3 0 0 1 B2 of 4 0 1$" /tmp/hudtest_calls.txt'
check "C code 37 at 37 m"                 'grep -q "^fake headunit hudtest 37 370 1 1 C code 37 0 1$" /tmp/hudtest_calls.txt'
check "ends by clearing"                  'tail -1 /tmp/hudtest_calls.txt | grep -q "hudtest 0 0 1 1 End 3 1$"'
check "dbusinfo run"                      'grep -q "^fake headunit dbusinfo" /tmp/hudtest_calls.txt'
check "usbinfo recorded"                  'grep -q "1d6b:0002" /usb/hud-test/aa_hud_test_log.txt'
check "installed AA unchanged"            'same $BIN/headunit /tmp/original_headunit && same $BIN/headunit-wrapper /tmp/original_wrapper'
check "rebooted"                          '[ -f /tmp/rebooted ]'

echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
