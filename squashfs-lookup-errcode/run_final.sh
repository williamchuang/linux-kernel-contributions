#!/bin/bash
# Verifies the tree that will actually be merged:
#   linux-next 84abb955b101 + Andrew Morton's two queued -fix patches
#   + 0001 (this patch) (directory header check) + Chen PATCH 1/2 (inode.c)
# Also shows Andrew's queue alone is still unsafe.  Run after run_all.sh.
set -e
H=$(cd "$(dirname "$0")" && pwd); T=/tmp/sqfs-rebuild/src; F=/tmp/sqfs-rebuild/fv
cd $T && git checkout -q -B b_akpm next && git apply $H/reconstruction/patches/akpm-11-fix.patch $H/reconstruction/patches/akpm-20-fix.patch \
  && git -c user.name=v -c user.email=v@v commit -qam akpm && git tag -f akpm >/dev/null
git checkout -q -B b_final akpm && git -c user.name=v -c user.email=v@v am -q $H/0001-*.patch \
  && git apply $H/reconstruction/patches/chen-1of2-inode.c.patch && git -c user.name=v -c user.email=v@v commit -qam chen1 && git tag -f final >/dev/null
cp "$H"/cbmc/* "$F"/; cd "$F"
NR='[("err = squashfs_read_metadata(dir->i_sb, &dirh,","sizeof(dirh)","R1 header read inside directory","dir"),("err = squashfs_read_metadata(dir->i_sb, dire, ","sizeof(*dire)","R2 entry read inside directory","dir"),("err = squashfs_read_metadata(dir->i_sb, dire->name,","size","R3 name read inside directory","dir")]'
DR='[("err = squashfs_read_metadata(inode->i_sb, &dirh,","sizeof(dirh)","R1d","inode"),("err = squashfs_read_metadata(inode->i_sb, dire, ","sizeof(*dire)","R2d","inode"),("err = squashfs_read_metadata(inode->i_sb, dire->name,","size","R3d","inode")]'
inst(){ python3 - "$1" "$2" <<'PY'
import sys
t=open(sys.argv[1]).read()
for key,sz,msg,v in eval(sys.argv[2]):
    i=t.index(key); ls=t.rfind('\n',0,i)+1; ind=t[ls:i]
    t=t[:ls]+ind+'__CPROVER_assert(length >= 0 && length + (loff_t)(%s) <= i_size_read(%s), "%s");\n'%(sz,v,msg)+t[ls:]
open(sys.argv[1],'w').write(t)
PY
}
induct(){ python3 - "$1" "$2" "$3" <<'PY'
import sys
t=open(sys.argv[1]).read(); var=sys.argv[3]; extra=" ctx->pos = (loff_t)nondet_u64();" if var=="inode" else ""; pos=" && ctx->pos >= 3" if var=="inode" else ""
a="\tfor (i = 0; i < i_count; i++) {"
t=t.replace(a,"\tif (nondet_bool()) {\n\t\ti = nondet_u32(); length = nondet_u32(); start_block = nondet_u32();\n\t\tindex_start = nondet_u64(); index_offset = nondet_int();\n\t\t__CPROVER_assume(i > 0 && i < i_count && index_offset >= 0 && index_offset < SQUASHFS_METADATA_SIZE);\n\t\tgoto K_IDX;\n\t}\n"+a+"\nK_IDX: ;",1)
b="\twhile (length < i_size_read(%s)) {"%var
t=t.replace(b,"\t{\n\t\tint k = nondet_int();\n\t\tif (k != 0) {\n\t\t\terr = nondet_int(); length = (loff_t)nondet_u64();\n\t\t\tblock = nondet_u64(); offset = nondet_int();\n\t\t\tdir_count = nondet_u32();"+extra+"\n\t\t\t{ unsigned char src[sizeof(dirh)]; memcpy(&dirh, src, sizeof(dirh)); }\n\t\t\t__CPROVER_assume(length >= 0 && length < i_size_read(%s) && offset >= 0 && offset < SQUASHFS_METADATA_SIZE && dir_count <= SQUASHFS_DIR_COUNT%s);\n\t\t\tif (k == 1) goto K_INNER;\n\t\t}\n\t}\n"%(var,pos)+b,1)
c="\t\twhile (dir_count--) {"; t=t.replace(c,c+"\nK_INNER: ;",1)
assert t.count('K_INNER')==2 and t.count('K_IDX')==2
open(sys.argv[2],'w').write(t)
PY
}
for t in akpm final; do
  git -C $T show $t:fs/squashfs/namei.c > n_$t.c; python3 extract.py n_$t.c '^static unsigned int get_dir_index_using_name' '^static struct dentry \*squashfs_lookup' > f_$t.c; inst f_$t.c "$NR"; induct f_$t.c fk_$t.c dir
done
git -C $T show final:fs/squashfs/dir.c > d_final.c; python3 extract.py d_final.c '^static unsigned int get_dir_index_using_offset' '^static int squashfs_readdir' > fd_final.c; inst fd_final.c "$DR"; induct fd_final.c fdk_final.c inode
git -C $T show final:fs/squashfs/inode.c > i_final.c; python3 extract.py i_final.c '^int squashfs_read_inode' > fi_final.c
python3 extract.py i_final.c '^struct inode \*squashfs_iget' > ig.c
git -C $T show final:fs/squashfs/cache.c > c_final.c; python3 extract.py c_final.c '^int squashfs_read_metadata' > rm.c
C="cbmc --object-bits 10 --bounds-check --pointer-check --signed-overflow-check"
run() { name=$1; shift; out="$H/results/$name.txt"; timeout 3000 "$@" > "$out" 2>&1 || true
  printf "%-36s %s %s\n" "$name" "$(grep -E '^VERIFICATION' "$out")" "$(grep -E '\*\* [0-9]+ of [0-9]+ failed' "$out")"; grep 'FAILURE$' "$out" | sed 's/^/      /'; }
echo "--- F. Andrew Morton's queue alone (expected to FAIL P1: header path)"
run F_akpm_bounded $C harness_namei.c -DFUNCS='"f_akpm.c"' -DISIZE_MAX=40 --unwind 6 --unwinding-assertions
run F_akpm_inductive $C harness_namei_k.c -DFUNCS='"fk_akpm.c"' --unwind 2
echo "--- G. final tree (akpm + 0001 (this patch) + Chen 1/2): all must pass"
run G_lookup_bounded $C harness_namei.c -DFUNCS='"f_final.c"' -DISIZE_MAX=40 --unwind 6 --unwinding-assertions
run G_lookup_inductive $C harness_namei_k.c -DFUNCS='"fk_final.c"' --unwind 2
run G_read_inode cbmc -I$T/fs/squashfs --bounds-check --pointer-check harness_inode.c -DFUNCS='"fi_final.c"' --unwind 4 --unwinding-assertions
run G_readdir_bounded $C harness_dir.c -DFUNCS='"fd_final.c"' --unwind 6 --unwinding-assertions
run G_readdir_inductive $C harness_dir_k.c -DFUNCS='"fdk_final.c"' --unwind 2
run G_contracts cbmc --bounds-check --pointer-check --signed-overflow-check harness_contracts.c --unwind 262 --unwinding-assertions
