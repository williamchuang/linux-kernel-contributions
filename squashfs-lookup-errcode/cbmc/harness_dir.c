#include "prelude.h"
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define SQUASHFS_MAX_DIR_TYPE 7
struct file { struct inode *inode; };
struct dir_context { loff_t pos; };
static inline struct inode *file_inode(struct file *f) { return f->inode; }
static bool dir_emit(struct dir_context *ctx, const char *name, int len, u64 ino, unsigned type)
{
	__CPROVER_assert(__CPROVER_r_ok(name, (size_t)len), "D1 emitted name is readable");
	return nondet_bool();
}
static const unsigned char squashfs_filetype_table[] = {
	DT_UNKNOWN, DT_DIR, DT_REG, DT_LNK, DT_BLK, DT_CHR, DT_FIFO, DT_SOCK
};
#include FUNCS
#ifndef ISIZE_MAX
#define ISIZE_MAX 40
#endif
int main(void)
{
	struct squashfs_sb_info sbi = { nondet_u64(), nondet_u64() };
	struct super_block sb = { &sbi };
	struct inode dir; dir.i_sb = &sb; dir.i_ino = nondet_u32();
	dir.i_size = nondet_u32();
	__CPROVER_assume(dir.i_size >= 3 && dir.i_size <= ISIZE_MAX);
	dir.sqi.start = nondet_u32(); dir.sqi.offset = nondet_int();
	__CPROVER_assume(dir.sqi.offset >= 0 && dir.sqi.offset < SQUASHFS_METADATA_SIZE);
	dir.sqi.dir_idx_start = nondet_u64(); dir.sqi.dir_idx_offset = nondet_int();
	__CPROVER_assume(dir.sqi.dir_idx_offset >= 0 && dir.sqi.dir_idx_offset < SQUASHFS_METADATA_SIZE);
	dir.sqi.dir_idx_cnt = nondet_u32(); __CPROVER_assume(dir.sqi.dir_idx_cnt <= 2);
	dir.sqi.parent = nondet_int();
	struct file f = { &dir };
	struct dir_context ctx; ctx.pos = nondet_u32(); __CPROVER_assume(ctx.pos <= ISIZE_MAX + 4);
	int r = squashfs_readdir(&f, &ctx);
	__CPROVER_assert(r == 0, "P4 readdir returns 0 (its error convention)");
	return 0;
}
