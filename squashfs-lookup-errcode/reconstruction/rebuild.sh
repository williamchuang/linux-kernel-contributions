#!/bin/bash
# Rebuild fs/squashfs as in linux-next 84abb955b101 from mainline + the posted series hunks,
# checking git blob hashes against the patches' own `index` lines.
set -e
D=$(cd "$(dirname "$0")" && pwd)
W=${1:-/tmp/sqfs-rebuild}
rm -rf "$W" && mkdir -p "$W" && cd "$W"
git clone -q --filter=blob:none --no-checkout https://github.com/torvalds/linux src
cd src && git sparse-checkout set fs/squashfs >/dev/null && git checkout -q 141b164bbe186423e295b139e40e8e2d11cf1341
ap() { git apply "$D/patches/$1"; }
for p in 02-namei.c 11-namei.c 20-namei.c 11-dir.c 20-dir.c 02-inode.c 07-inode.c 10-inode.c 12-inode.c 15-inode.c 19-inode.c 20-inode.c; do ap $p.patch; done
chk() { h=$(git hash-object fs/squashfs/$1 | cut -c1-${#2}); [ "$h" = "$2" ] && echo "OK   $1 $h" || echo "DIFF $1 $h (expected $2)"; }
chk dir.c 33b8225fcdaa; chk inode.c 5404d7ba6fc0
chk namei.c 5f8decee2   # = base blob in Chen's PATCH 2/2 (5f8decee2), i.e. linux-next
git add -A >/dev/null; git -c user.name=v -c user.email=v@v commit -qm next; git tag next
ap chen-1of2-inode.c.patch; ap chen-2of2-namei.c.patch
chk inode.c 4cf8a41b5; chk namei.c 5dc21ddaf
git -c user.name=v -c user.email=v@v commit -qam chen; git tag chen
echo "tree at $W/src  (tags: next, chen)"
