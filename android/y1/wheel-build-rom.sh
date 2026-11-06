#!/bin/bash
# Y1 Wheel: build one full-flash ROM zip, the same way as
# .github/workflows/build-roms.yml does for the Rockbox-Y1 nightlies.
#
#   android/y1/wheel-build-rom.sh <base-release-tag> <output.zip>
#   e.g.  android/y1/wheel-build-rom.sh type-a-base rom_360p.zip
#
# Run from the repository root after the "build rockbox" step, so that
# android/y1/build holds rockbox.apk, libs/armeabi/*, gocurl and poddl.
# Needs sudo (loop-mounts the ext4 images) and network access to GitHub.
set -euo pipefail

BASE_TAG=$1
OUT=$2
ROOT=$PWD
BUILD=$ROOT/android/y1/build
SCRIPTS=$ROOT/android/y1/scripts
REL=https://github.com/rockbox-y1/rockbox/releases/download
WORK=$ROOT/rom-$BASE_TAG

[ -f "$BUILD/rockbox.apk" ] && [ -f "$BUILD/libs/armeabi/librockbox.so" ] || {
    echo "build android/y1/build first" >&2; exit 1; }

# Bluetooth driver fix shipped by the upstream ROMs
for f in libbluetoothdrv.so libbluetoothdrv_real.so; do
    [ -f "$ROOT/$f" ] || wget -q -O "$ROOT/$f" "$REL/airpods-2-fix/$f"
done
[ -f "$ROOT/cacert.pem" ] || wget -q -O "$ROOT/cacert.pem" https://curl.se/ca/cacert.pem

rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"
wget -q "$REL/$BASE_TAG/rom.zip"
wget -q "$REL/stock-menu/better-Y_3.1.2_v1.1.0-dev1_patched.apk"
cp better-Y_*.apk com.innioasis.y1.apk
unzip -q rom.zip
mkdir mount_sys mount_user
sudo mount -t ext4 -o loop system.img mount_sys
sudo mount -t ext4 -o loop userdata.img mount_user
trap 'cd "$WORK"; sudo umount mount_sys mount_user 2>/dev/null || true' EXIT

sudo cp com.innioasis.y1.apk mount_user/data/com.innioasis.y1.apk
sudo cp "$SCRIPTS/LeanKeyKeyboardPro_v5.2.15_r.apk" mount_user/data/LeanKeyKeyboardPro_v5.2.15_r.apk
sudo cp "$BUILD/rockbox.apk" mount_sys/app/org.rockbox.apk
sudo chmod 644 mount_sys/app/org.rockbox.apk
sudo chown root:root mount_sys/app/org.rockbox.apk
sudo mkdir -p mount_sys/lib
sudo cp "$BUILD/libs/armeabi/librockbox.so" mount_sys/lib/.
sudo chmod 644 mount_sys/lib/librockbox.so
sudo chown root:root mount_sys/lib/librockbox.so
sudo rm -rf mount_user/data/org.rockbox/lib
sudo mkdir -p mount_user/data/org.rockbox/lib
sudo cp "$BUILD"/libs/armeabi/* mount_user/data/org.rockbox/lib/.
sudo chmod 777 mount_user/data/org.rockbox/lib/*
sudo rm -rf mount_user/data/org.rockbox/app_rockbox
sudo mkdir -p mount_user/data/org.rockbox/app_rockbox/.rockbox/docs
sudo cp "$ROOT/docs/COPYING" mount_user/data/org.rockbox/app_rockbox/.rockbox/docs/COPYING.txt
sudo cp "$ROOT/docs/LICENSES" mount_user/data/org.rockbox/app_rockbox/.rockbox/docs/LICENSES.txt
unzip -q "$BUILD/libs/armeabi/libmisc.so"
for f in sdcard/.rockbox/*; do
    sudo cp -rf "$f" mount_user/data/org.rockbox/app_rockbox/.rockbox/
done

sudo cp "$SCRIPTS/install-recovery.sh" mount_sys/etc/install-recovery.sh
sudo chmod 755 mount_sys/etc/install-recovery.sh
sudo mkdir -p mount_sys/etc/init.d
sudo cp "$SCRIPTS/99Y1ButtonScript" mount_sys/etc/init.d/
sudo chmod 755 mount_sys/etc/init.d/99Y1ButtonScript
sudo cp "$SCRIPTS/switch-to-stock.sh" mount_user/data/

sudo cp "$BUILD/gocurl/gocurl" mount_user/data
sudo chmod 755 mount_user/data/gocurl
sudo cp "$BUILD/download-podcast/poddl" mount_user/data
sudo chmod 755 mount_user/data/poddl
sudo cp "$ROOT/cacert.pem" mount_user/data

sudo cp "$SCRIPTS/Rockbox.kl" mount_sys/usr/keylayout/Rockbox.kl
sudo cp "$SCRIPTS/Stock.kl" mount_sys/usr/keylayout/Stock.kl
sudo cp "$SCRIPTS/Rockbox.kl" mount_sys/usr/keylayout/Generic.kl

sudo mkdir -p mount_user/data/update
sudo cp "$SCRIPTS/update.sh" mount_user/data/update/update.sh

sudo cp "$SCRIPTS/bootanimation.zip" mount_sys/media/bootanimation.zip

sudo cp "$ROOT/libbluetoothdrv.so" mount_sys/lib/.
sudo cp "$ROOT/libbluetoothdrv_real.so" mount_sys/lib/.
sudo chmod 644 mount_sys/lib/libbluetoothdrv.so
sudo chmod 644 mount_sys/lib/libbluetoothdrv_real.so

sudo umount mount_sys
sudo umount mount_user
trap - EXIT
rm -f rom.zip com.innioasis*.apk better-Y_*.apk
rm -rf mount_sys mount_user sdcard
cd "$ROOT"
rm -f "$OUT"
zip -j "$OUT" "$WORK"/*
rm -rf "$WORK"
echo "ROM written to $OUT"
