# Changelog

## 1.14.1 (2026-10-09)

- **USB update packages** in `mazda/usb-update/` and attached to the release: install, restore,
  collect-logs, cleanup and HUD test, ready to unzip onto a USB stick. They update only the `headunit`
  program (no full reinstall), keep the previous version for restoring, restore it automatically if the new
  version doesn't start, and need no taps on the screen. See [`mazda/usb-update/README.md`](mazda/usb-update/README.md).
  Works on firmware where USB tweaks run (tested on 56.00.511); not on 59.00.502+ / 70.xx+.
- Popups show proper messages ("Installing Android Auto 1.14.1") instead of test-build wording, and the
  install no longer switches debug logging on.
- `make usb-packages` builds the zips; `test-usb-scripts.sh` tests them against a fake CMU.
- No changes to the headunit program itself compared to 1.14.

## 1.14 (2026-10-09)

Fixes for the HUD navigation display and for Android Auto not (re)connecting, found and tested in a real car.

### Tested on

| | |
|---|---|
| Car | Mazda Axela (Mazda3) 2014 hybrid, with HUD (Active Driving Display) |
| CMU firmware | 56.00.511 (`MAZ_CMU-150_56.00.511`) |
| Previous headunit build | v1.12-11-g9cadfdc (installed with MZD-AIO tweaks) |
| Phone | Android 16, Android Auto 17.7 |
| Connection | USB, through a generic wireless Android Auto adaptor (2-in-1 CarPlay/AA box) |

Two test drives on the same route, plus HUD tests with a camera on the HUD (every icon code, distances
from 10 m to 15 km).

### HUD

- **HUD froze on the first arrow (often the start flag) or showed nothing.** The HUD thread exited on the
  first dbus error, and on 56.00.511 every update causes one because `SetHUD_Display_Msg2` (street names)
  doesn't exist. The thread now keeps running for the whole session, retries failed updates, and waits for
  the HUD service instead of giving up.
- **`SetHUD_Display_Msg2` is optional:** tried once, then skipped for the session if the firmware doesn't
  have it. Street names can't be shown on this firmware (no other method for it exists).
- **HUD kept the last arrow after the phone disconnected** until the car restarted. It is now cleared when
  the phone disconnects or navigation stops.
- One mutex for the navigation data (`main.cpp` passed its own, so updates could be lost).
- The HUD message counter changes once per update; it could wrap back to the previous value, which the
  HUD ignores.
- Icon lookup is bounds checked (turn side 0 after navigation stopped read outside the table).
- Roundabout exits of 345-360° no longer show the other driving side's icons; roundabout enter/exit
  events get an exit icon when Android Auto sends the angle.
- Only changes are sent to the HUD (it keeps showing the last message).
- Note: the HUD only displays guidance when the next manoeuvre is within ~300 m (like Mazda's own
  navigation), so long straight stretches stay blank until the next turn is near. The icon table and the
  distance encoding were confirmed correct on this HUD.

### Connecting and reconnecting (USB)

- **Android Auto often didn't start after a cold start, and replugging didn't help.**
  - When nothing answered the first USB scan, the headunit waited forever for a new device instead of
    rescanning; it now rescans every 2 s and can't miss a device that re-enumerates during a scan.
  - Worker threads were stopped with a condition-variable notify that is lost if the thread isn't waiting,
    so the end of a session could hang forever (always when the phone dropped within 0.5 s). Replaced with
    a `QuitSignal` that can't be missed.
  - A disconnect right after the handshake could leave the main loop running forever; the loop is now
    created before connecting.
  - A failed shutdown message recursed until the stack overflowed.
- **Wireless adaptor didn't come back after the phone was out of range.** Some adaptors drop off USB and
  only return when replugged. If the adaptor's hub port is still empty 30 s after a session ended, that
  one port's power is switched off and on (every 2 minutes, at most 10 times). Only that port, only while
  nothing is connected to it, and only on hubs with per-port power switching.
- The USB interface is claimed with a short retry (it failed on every connect right after the switch to
  accessory mode).
- Device handles and udev monitors are no longer leaked.

### Wi-Fi transport

- Works on current Linux kernels: `select()` timeouts of a second or more were rejected with `EINVAL`.
- A closed connection (`read()` returning 0) is detected instead of spinning at 100% CPU.
- A TCP read that returns part of a frame header is completed instead of dropping the session.
- No SIGPIPE crash when the phone has already hung up.

### Diagnostics and tools

- More logging: every change sent to the HUD and every displayed distance.
- `headunit usbinfo`, `headunit dbusinfo` and `headunit hudtest` for testing from a USB stick.
- `HU_DEBUG=1` enables debug logs.
- HUD simulator (`tools/hud-sim`, desktop build with `make HUD_SIM=1`), scripted drives
  (`mazda/hud/test/hud_replay`), HUD logic unit tests (`make -C mazda/hud/test`) and a fake phone for
  connection tests (`tools/fake-phone`).
