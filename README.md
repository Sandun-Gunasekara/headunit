# Unofficial Mazda Connect (tm) (*) Android Auto Headunit App

Unofficial port of Android Auto Headunit App to Mazda Connect CMU. The app makes extensive use of jni functions that were originally developed by Mike Reid as part of his Android app. The Mazda specific source code is under the /mazda folder.

(*) - Mazda and Mazda Connect are trademarks of Mazda NA

The goal of this fork is to update the code to modernize it, and to use libprotobuf instead of manually parsing the messages so that it will be easier to add more complete integration with car systems.


# Installing on the car

To install or update Android Auto on a CMU that already has it (e.g. from MZD-AIO), use the USB stick packages attached to each [release](https://github.com/Sandun-Gunasekara/headunit/releases); see [mazda/usb-update/README.md](mazda/usb-update/README.md) for the steps and supported firmware.
Release notes: [CHANGELOG.md](CHANGELOG.md).
