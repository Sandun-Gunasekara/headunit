# Updating Android Auto from a USB stick

These USB packages update the Android Auto headunit on the Mazda Connect CMU **without the full
installer**: only the `headunit` program is replaced, the previous version is kept so it can be restored,
and nothing in the Mazda system files (`/jci`, boot scripts, GUI) is touched.

Every script runs from start to finish by itself — no taps or joystick input are needed. Popups on the
screen only show progress.

## ⚠️ Before you start: requirements and firmware

- **Android Auto must already be installed** on the CMU (for example with the MZD-AIO tweaks), because
  only the `headunit` program is replaced. If it isn't found, the update stops and changes nothing.
- **Tested only on CMU firmware 56.00.511** (Mazda Axela/Mazda3 2014). It should work on firmware where the
  MZD-AIO tweaks USB install works (55.xx, 56.xx, 59.00 up to 59.00.4xx).
- **It will not work on newer firmware: 59.00.502 and later, and 70.xx and later.** That firmware ignores
  USB install scripts unless the CMU has already been opened up (serial connection / autorun). Nothing
  happens when you plug in the stick, and nothing is changed.
- Check your firmware: **Settings → System → About / Version information**.

## Downloads (from the release)

| File | Use |
|---|---|
| `headunit-<version>-usb-install.zip` | **Install / update** Android Auto |
| `headunit-<version>-usb-restore.zip` | Put back the version that was installed before the first update |
| `headunit-<version>-usb-collect-logs.zip` | Copy the Android Auto logs to the stick (changes nothing) |
| `headunit-<version>-usb-cleanup.zip` | After you're happy: remove the backup of the previous version from the car |
| `headunit-<version>-usb-hud-test.zip` | Diagnostics only: test patterns on the HUD (changes nothing) |
| `headunit-<version>` | The program on its own (for manual installs) |
| `headunit-<version>-SHA256SUMS.txt` | Checksums |

## Preparing a USB stick

1. Use a small USB stick (32 GB or less). Format it as **FAT32** with **MBR / Master Boot Record**
   (not exFAT, not GUID).
   - macOS: Disk Utility → Erase → Format *MS-DOS (FAT)*, Scheme *Master Boot Record*.
   - Windows: right-click the drive → Format → *FAT32*.
2. Unzip the package and copy **the files inside it** (not a folder) to the **top level** of the stick. The
   stick should show `tweaks.sh`, `cmu_dataretrieval.up`, `dataRetrieval_config.txt`, `jci-autoupdate`
   (and `headunit` for install).
3. **macOS: remove the hidden `._` files** Finder adds, otherwise the CMU ignores the stick
   (`._cmu_dataretrieval.up` blocks it). In Terminal: `dot_clean -m /Volumes/<stick>` then
   `rm -f /Volumes/<stick>/._*`, or use `prepare-stick.sh` from this folder.
4. Eject the stick properly.

## Installing / updating

1. Park with the **engine running** (so the battery doesn't drain). Unplug the phone or Android Auto adaptor.
2. Wait until the CMU home screen is fully up.
3. Plug in the install stick. Within 1–2 minutes you'll see **"Installing Android Auto &lt;version&gt;. Do not
   remove the USB drive."** Don't touch anything.
4. About a minute later: **"Android Auto &lt;version&gt; installed. Remove the USB drive; the system will reboot
   now."** Remove the stick; the CMU reboots by itself.
5. Connect your phone or adaptor as usual.

What can happen instead:

- **No popup after 3 minutes:** the CMU doesn't run USB scripts (firmware too new, or the stick isn't
  prepared correctly — check the files are at the top level and the `._` files are gone). Nothing was
  changed.
- **"... did not start, so the previous version was restored":** the new version couldn't run on this CMU;
  your previous version is back. The stick has `aa_install_log.txt` explaining why.
- **Any other error:** nothing was changed; see `aa_install_log.txt` on the stick.

Keep the stick: it now has `aa_install_log.txt` and `backup_original/`, a copy of the version you had
before. The car also keeps that version as `headunit.orig` (saved on the first update only, so it stays your
original version even if you update again).

## Going back to the previous version

Prepare a stick with the **restore** package, plug it in the same way and wait for **"Original version
restored"**; the CMU reboots. If the car no longer has `headunit.orig` (e.g. after cleanup), copy the
`backup_original/` folder from your install or cleanup stick onto the restore stick, next to `tweaks.sh`.

## Logs

The **collect-logs** package copies `/data/headunit.log`, the installed files, free space and system
messages into an `aa_logs_<date>` folder on the stick. It changes nothing and doesn't reboot.
Debug logging is only on if `DEBUG=1` is set in `/tmp/mnt/data_persist/dev/bin/headunit-wrapper`.

## Cleanup (optional)

Once you're happy with the new version, the **cleanup** package copies the previous version from the car to
the stick (`backup_original/`), deletes `headunit.orig` / `headunit-wrapper.orig` and the debug log from the
car, switches debug logging off and reboots. Keep that stick: it's now your copy of the previous version.

## For developers

- Build the packages: `make usb-packages` in `mazda/` (needs the m3-toolchain); zips go to
  `mazda/usb-update/dist/`.
- Test the scripts against a fake CMU with busybox (in the Docker dev image):
  `docker run --rm -v "$PWD:/src:ro" <image> bash /src/mazda/usb-update/test-usb-scripts.sh`
