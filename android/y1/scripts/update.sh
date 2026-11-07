echo "++++ Starting system update"

RB=/sdcard/.rockbox
UPD=$RB/update

# Remove a folder tree. On the Y1's card a single "rm -rf" can stop with
# "rm failed for update/, Directory not empty", so try busybox and toolbox
# a few times and, unless told to keep it, move what is left out of the way.
remove_tree() {
  for i in 1 2 3; do
    [ -e "$1" ] || return 0
    busybox rm -rf "$1" 2>/dev/null || rm -rf "$1" 2>/dev/null
  done
  [ -e "$1" ] || return 0
  [ "$2" = keep ] && return 1
  mv "$1" "$1.old.$$" 2>/dev/null
  [ -e "$1" ] && return 1
  return 0
}

stop() {
  echo "!! $1"
  echo "!! Update stopped. Nothing on the system was changed."
  remove_tree "$UPD"
  exit 1
}

cd "$RB" || stop "can't open $RB"

# a leftover update folder from an earlier update makes unzip stop with
# "can't create directory ... File exists" before the important files
remove_tree "$UPD" || stop "can't clear the old $UPD folder"
for d in "$RB"/update.old.*; do remove_tree "$d" keep; done

[ -s update.zip ] || stop "update.zip not found"
unzip -o update.zip
cd "$UPD" || stop "update.zip did not unpack"
unzip -o libs/armeabi/libmisc.so
echo "++ Unpacked update"

# check everything is here before anything is replaced
for f in libs/armeabi/librockbox.so libs/armeabi/libmisc.so rockbox.apk \
         update.sh install-recovery.sh 99Y1ButtonScript Rockbox.kl Stock.kl \
         sdcard/.rockbox/rockbox-info.txt; do
  [ -s "$UPD/$f" ] || stop "missing $f"
done
echo "++ Checked update files"

mount -o rw,remount /system
echo "++ Remounted /system"

cp $UPD/libs/armeabi/librockbox.so /system/lib/
chmod 644 /system/lib/librockbox.so
chown root:root /system/lib/librockbox.so
remove_tree /data/data/org.rockbox/lib
mkdir -p /data/data/org.rockbox/lib
for f in $UPD/libs/armeabi/*; do
  cp "$f" /data/data/org.rockbox/lib/
done
chmod 777 /data/data/org.rockbox/lib/*
for f in $UPD/sdcard/.rockbox/*; do
  cp -rf "$f" $RB/
done
echo "++ Updated libraries"

rm /system/app/org.rockbox.apk
cp $UPD/rockbox.apk /system/app/org.rockbox.apk
chmod 644 /system/app/org.rockbox.apk
chown root:root /system/app/org.rockbox.apk
echo "++ Updated rockbox apk"

cp $UPD/install-recovery.sh /system/etc/install-recovery.sh
chmod 755 /system/etc/install-recovery.sh
mkdir -p /system/etc/init.d
cp $UPD/99Y1ButtonScript /system/etc/init.d/
chmod 755 /system/etc/init.d/99Y1ButtonScript
cp $UPD/switch-to-stock.sh /data/data/
cp $UPD/gocurl /data/data/
chmod 755 /data/data/gocurl
cp $UPD/poddl /data/data/
chmod 755 /data/data/poddl
mkdir -p /data/data/update
cp $UPD/update.sh /data/data/update/update.sh
echo "++ Updated scripts"

cp $UPD/Rockbox.kl /system/usr/keylayout/Rockbox.kl
cp $UPD/Stock.kl /system/usr/keylayout/Stock.kl
cp $UPD/Rockbox.kl /system/usr/keylayout/Generic.kl
echo "++ Updated keylayouts"

# last: installing the Innioasis app takes minutes; if it is cut short
# (restart, power) everything Rockbox needs is already in place
if ls $UPD/com.innioasis.y1*.apk >/dev/null 2>&1; then
  pm install $UPD/com.innioasis.y1*.apk
  echo "++ Updated stock apk"
fi

cd $RB
remove_tree "$UPD" || echo "!! could not remove $UPD (delete it by hand)"
echo "++ Cleaned up"

echo "++++ Finished update."
reboot
