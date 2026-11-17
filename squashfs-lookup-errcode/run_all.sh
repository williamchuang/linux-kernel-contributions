#!/bin/bash
# Reproduces every result in REPORT.md.  Needs: git, python3, cbmc (5.95+), gcc.
set -e
H=$(cd "$(dirname "$0")" && pwd)
W=/tmp/sqfs-rebuild; bash "$H/reconstruction/rebuild.sh" "$W"
T=$W/src; F=$W/fv; rm -rf "$F"; mkdir -p "$F"; cp "$H"/cbmc/* "$F"/; cd "$F"
inst() { # file key size msg var
python3 - "$1" <<PY
import sys
t=open(sys.argv[1]).read()
for key,sz,msg,v in $2:
    i=t.index(key); ls=t.rfind('\n',0,i)+1; ind=t[ls:i]
    t=t[:ls]+ind+'__CPROVER_assert(length >= 0 && length + (loff_t)(%s) <= i_size_read(%s), "%s");\n'%(sz,v,msg)+t[ls:]
open(sys.argv[1],'w').write(t)
PY
}
NR='[("err = squashfs_read_metadata(dir->i_sb, &dirh,","sizeof(dirh)","R1 header read inside directory","dir"),("err = squashfs_read_metadata(dir->i_sb, dire, ","sizeof(*dire)","R2 entry read inside directory","dir"),("err = squashfs_read_metadata(dir->i_sb, dire->name,","size","R3 name read inside directory","dir")]'
DR='[("err = squashfs_read_metadata(inode->i_sb, &dirh,","sizeof(dirh)","R1d","inode"),("err = squashfs_read_metadata(inode->i_sb, dire, ","sizeof(*dire)","R2d","inode"),("err = squashfs_read_metadata(inode->i_sb, dire->name,","size","R3d","inode")]'
for t in next chen; do
  git -C $T show $t:fs/squashfs/namei.c > n_$t.c; python3 extract.py n_$t.c '^static unsigned int get_dir_index_using_name' '^static struct dentry \*squashfs_lookup' > f_$t.c; inst f_$t.c "$NR"
  git -C $T show $t:fs/squashfs/dir.c > d_$t.c; python3 extract.py d_$t.c '^static unsigned int get_dir_index_using_offset' '^static int squashfs_readdir' > fd_$t.c; inst fd_$t.c "$DR"
  git -C $T show $t:fs/squashfs/inode.c > i_$t.c; python3 extract.py i_$t.c '^int squashfs_read_inode' > fi_$t.c
done
sed 's/\tu64 block = squashfs_dir_start(squashfs_i(dir)->start, msblk);/\ts64 block = squashfs_dir_start(squashfs_i(dir)->start, msblk);/' f_chen.c > f_s64.c
python3 - <<'PY'
for t in ('next','chen'):
    s=open('f_%s.c'%t).read()
    a="\tfor (i = 0; i < i_count; i++) {"
    s=s.replace(a,"\tif (nondet_bool()) {\n\t\ti = nondet_u32(); length = nondet_u32(); start_block = nondet_u32();\n\t\tindex_start = nondet_u64(); index_offset = nondet_int();\n\t\t__CPROVER_assume(i > 0 && i < i_count && index_offset >= 0 && index_offset < SQUASHFS_METADATA_SIZE);\n\t\tgoto K_IDX;\n\t}\n"+a+"\nK_IDX: ;",1)
    b="\twhile (length < i_size_read(dir)) {"
    s=s.replace(b,"\t{\n\t\tint k = nondet_int();\n\t\tif (k != 0) {\n\t\t\terr = nondet_int(); length = (loff_t)nondet_u64();\n\t\t\tblock = nondet_u64(); offset = nondet_int();\n\t\t\tdir_count = nondet_u32();\n\t\t\t{ unsigned char src[sizeof(dirh)]; memcpy(&dirh, src, sizeof(dirh)); }\n\t\t\t__CPROVER_assume(length >= 0 && length < i_size_read(dir) && offset >= 0 && offset < SQUASHFS_METADATA_SIZE && dir_count <= SQUASHFS_DIR_COUNT);\n\t\t\tif (k == 1) goto K_INNER;\n\t\t}\n\t}\n"+b,1)
    c="\t\twhile (dir_count--) {"
    s=s.replace(c,c+"\nK_INNER: ;",1)
    open('fk_%s.c'%t,'w').write(s)
PY
C="cbmc --object-bits 10 --bounds-check --pointer-check --signed-overflow-check"
run() { name=$1; shift; out="$H/results/$name.txt"; timeout 3000 "$@" > "$out" 2>&1 || true
  printf "%-46s %s %s\n" "$name" "$(grep -E '^VERIFICATION' "$out")" "$(grep -E '\*\* [0-9]+ of [0-9]+ failed' "$out")"; grep 'FAILURE$' "$out" | sed 's/^/      /'; }
echo "--- A. squashfs_lookup, bounded (i_size<=40, <=2 index entries, all loops fully unwound)"
for t in next chen; do for m in 0 1; do run "A_lookup_${t}_iget$m" $C harness_namei.c -DFUNCS="\"f_$t.c\"" -DIGET_MODE=$m -DISIZE_MAX=40 --unwind 6 --unwinding-assertions; done; done
run A_lookup_chen_plus_s64_block $C harness_namei.c -DFUNCS='"f_s64.c"' -DISIZE_MAX=40 --unwind 6 --unwinding-assertions
echo "--- B. squashfs_lookup, inductive (any i_size < 2^32, any index count)"
for t in next chen; do run "B_lookup_inductive_$t" $C harness_namei_k.c -DFUNCS="\"fk_$t.c\"" --unwind 2; done
echo "--- C. squashfs_read_inode, all inode types"
for t in next chen; do run "C_read_inode_$t" cbmc -I$T/fs/squashfs --bounds-check --pointer-check harness_inode.c -DFUNCS="\"fi_$t.c\"" --unwind 4 --unwinding-assertions; done
echo "--- D. squashfs_readdir, bounded"
for t in next chen; do run "D_readdir_$t" $C harness_dir.c -DFUNCS="\"fd_$t.c\"" --unwind 6 --unwinding-assertions; done
echo "--- E. compiler: dead check"
printf '#define __CPROVER_assert(c,m) ((void)0)\n#define __CPROVER_assume(c) ((void)0)\n#define __CPROVER_w_ok(p,n) 1\n#define __CPROVER_r_ok(p,n) 1\n' > shim.h
printf '#include "prelude.h"\n#include "f_chen.c"\n' | gcc -include shim.h -Wtype-limits -fsyntax-only -x c - 2>&1 | grep "always false" | tee "$H/results/E_gcc_type_limits.txt" || true
