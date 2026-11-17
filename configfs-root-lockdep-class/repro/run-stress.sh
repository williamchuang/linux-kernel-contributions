#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# usage: run-stress.sh <bzImage> <logfile>
# Runs stress.c (three concurrent workers, 60 s) as PID 1 under QEMU.
set -e
kernel=$(realpath "$1"); log=$(realpath -m "$2")
cd "$(dirname "$0")"
rm -rf stress-initramfs && mkdir -p stress-initramfs/dev stress-initramfs/proc stress-initramfs/sys
gcc -static -O2 -Wall -o stress-initramfs/init stress.c
(cd stress-initramfs && find . | cpio -o -H newc --quiet) > stress.cpio
timeout 900 qemu-system-x86_64 -m 1024 -smp 2 -nographic -no-reboot \
	-kernel "$kernel" -initrd stress.cpio \
	-append "console=ttyS0 panic=-1 rdinit=/init" </dev/null >"$log" 2>&1 || true
grep -E "STRESS|possible circular|hung task" "$log" || true
