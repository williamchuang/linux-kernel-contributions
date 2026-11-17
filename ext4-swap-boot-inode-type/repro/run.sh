#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# usage: run.sh <uml-kernel> <fresh|dir|chrdev> <logfile>
# Makes a fresh ext4 image, optionally sets the mode of inode 5 (the boot
# loader inode) with debugfs, boots UML, runs EXT4_IOC_SWAP_BOOT twice,
# then runs e2fsck -fn on the image.
set -e
cd "$(dirname "$0")"
mkdir -p root/dev root/proc root/mnt/a
gcc -static -O2 -Wall -o root/init init.c
rm -f a.img; truncate -s 64M a.img
mke2fs -q -F -t ext4 a.img
case "$2" in
	dir)    debugfs -w -R "sif <5> mode 040755" a.img >/dev/null 2>&1
		debugfs -w -R "sif <5> links_count 1" a.img >/dev/null 2>&1 ;;
	chrdev) debugfs -w -R "sif <5> mode 020644" a.img >/dev/null 2>&1
		debugfs -w -R "sif <5> links_count 1" a.img >/dev/null 2>&1 ;;
	fresh)  ;;
esac
debugfs -R "stat <5>" a.img 2>/dev/null | head -2 | sed 's/^/IMAGE: /' > "$3.pre"
timeout 300 "$1" mem=256M ubd0=a.img root=/dev/root rootfstype=hostfs \
	hostfs="$PWD/root" rw init=/init con=null con0=fd:0,fd:1 </dev/null >"$3" 2>&1 || true
e2fsck -fn a.img > "${3%.log}.fsck" 2>&1 || true
debugfs -R "stat <5>" a.img 2>/dev/null | head -2 | sed 's/^/AFTER: /' >> "$3.pre"
cat "$3.pre"; rm -f "$3.pre"
grep -E "REPRO|WARNING|lock_two_nondirectories|boot loader|EXT4-fs error" "$3" | sed 's/\[<[0-9a-f]*>\] //' | head -12 || true
