#include "prelude_inode.h"
#include FUNCS
int main(void)
{
	struct squashfs_sb_info sbi = { nondet_u64(), nondet_u64(), nondet_u32(), nondet_bool() ? &sbi : 0 };
	__CPROVER_assume(sbi.block_size >= 4096 && sbi.block_size <= (1u << 20) && (sbi.block_size & (sbi.block_size - 1)) == 0);
	/* super.c mount checks: inode_table < directory_table <= next_table <= bytes_used <= bdev size */
	__CPROVER_assume(sbi.directory_table <= (u64)S64_MAX && sbi.inode_table < sbi.directory_table);
	struct super_block sb = { &sbi };
	struct inode inode = { 0 };
	inode.i_sb = &sb;
	long long ino = (long long)nondet_u64();
	int r = squashfs_read_inode(&inode, ino);
	__CPROVER_assert(r == 0 || (r < 0 && r >= -MAX_ERRNO),
		"P3 squashfs_read_inode returns 0 or -errno (so squashfs_iget never builds ERR_PTR(positive))");
	return 0;
}
