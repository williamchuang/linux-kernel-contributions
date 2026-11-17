#include "prelude.h"
#include FUNCS
#ifndef ISIZE_MAX
#define ISIZE_MAX 64
#endif
#ifndef IDX_MAX
#define IDX_MAX 2
#endif
int main(void)
{
	struct squashfs_sb_info sbi = { nondet_u64(), nondet_u64() };
	struct super_block sb = { &sbi };
	struct inode dir;
	dir.i_sb = &sb;
	dir.i_size = nondet_u32();
	/* inode.c (patch 20) guarantees i_size >= 3 for any directory reaching lookup */
	#ifndef ISIZE_MIN
#define ISIZE_MIN 3
#endif
	__CPROVER_assume(dir.i_size >= 3 && dir.i_size <= 0xffffffffLL);
	dir.sqi.start = nondet_u32();
	dir.sqi.offset = nondet_int();
	__CPROVER_assume(dir.sqi.offset >= 0 && dir.sqi.offset < SQUASHFS_METADATA_SIZE);
	dir.sqi.dir_idx_start = nondet_u64();
	dir.sqi.dir_idx_offset = nondet_int();
	__CPROVER_assume(dir.sqi.dir_idx_offset >= 0 && dir.sqi.dir_idx_offset < SQUASHFS_METADATA_SIZE);
	dir.sqi.dir_idx_cnt = nondet_u32();
	

	static unsigned char name[SQUASHFS_NAME_LEN + 2];
	unsigned int len = nondet_u32();
	__CPROVER_assume(len >= 1 && len <= SQUASHFS_NAME_LEN + 1);
	{ unsigned char src[sizeof(name)]; memcpy(name, src, sizeof(name)); }
	name[len] = 0;
	struct dentry de = { { name, len } };

	struct dentry *r = squashfs_lookup(&dir, &de, 0);

	__CPROVER_assert(r == NULL || r == &g_alias || IS_ERR(r),
		"P1 lookup returns NULL, a real dentry, or ERR_PTR(-errno) - never a small bogus pointer");
	return 0;
}
