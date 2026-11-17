#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# usage: run.sh <bzImage> <logfile>
# Builds the initramfs from init.c and boots it under QEMU (TCG, no KVM).
set -e
kernel=$(realpath "$1"); log=$(realpath -m "$2")
cd "$(dirname "$0")"
mkdir -p initramfs/dev initramfs/proc initramfs/sys
gcc -static -O2 -Wall -o initramfs/init init.c
(cd initramfs && find . | cpio -o -H newc --quiet) > initramfs.cpio
timeout 900 qemu-system-x86_64 -m 1024 -smp 2 -nographic -no-reboot \
	-kernel "$kernel" -initrd initramfs.cpio \
	-append "console=ttyS0 panic=-1 rdinit=/init" </dev/null >"$log" 2>&1 || true
grep -E "REPRO|possible circular|is trying to acquire|already holding|-> #|DEADLOCK" "$log" || true
