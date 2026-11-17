#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Boot a UML kernel with fresh ext4 images and run one reproducer.
# usage: run.sh <uml-kernel> <swap|inline_of|inline_sb|inline_user> <logfile>
set -e
cd "$(dirname "$0")"
mkdir -p root/dev root/proc root/sys root/mnt/a root/mnt/b
gcc -static -O2 -Wall -o root/init init.c
rm -f a.img b.img c.img
truncate -s 64M a.img b.img c.img
mke2fs -q -F -t ext4 -O quota a.img                   # swap-boot path
mke2fs -q -F -t ext4 -O inline_data,orphan_file b.img # inline path, orphan file
mke2fs -q -F -t ext4 -O inline_data c.img             # inline path, superblock orphan list
timeout 300 "$1" mem=256M ubd0=a.img ubd1=b.img ubd2=c.img \
	root=/dev/root rootfstype=hostfs hostfs="$PWD/root" rw init=/init \
	REPRO="$2" con=null con0=fd:0,fd:1 </dev/null >"$3" 2>&1 || true
case "$2" in swap) img=a ;; inline_sb) img=c ;; *) img=b ;; esac
e2fsck -fn $img.img > "${3%.log}.fsck" 2>&1 || true
grep -E "REPRO: (inline: pwrite|swap_boot: ioctl)|WARNING: fs/jbd2|credits" "$3" || true
