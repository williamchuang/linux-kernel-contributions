#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
ln -s /lib/modules/any /lib/modules/$(uname -r)
echo "ST: running configfs kselftest on $(uname -r)"
/configfs_test
echo "ST: kselftest exit status $?"
echo "ST: lockdep/warning lines in dmesg:"
dmesg | grep -E "circular locking|possible recursive|inconsistent lock|WARNING:|BUG:" || echo "ST: none"
echo "ST: warn_count=$(cat /sys/kernel/warn_count 2>/dev/null)"
sync
poweroff -f
