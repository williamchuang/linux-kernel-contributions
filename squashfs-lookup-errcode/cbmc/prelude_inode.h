/* Environment model for squashfs_read_inode() (fs/squashfs/inode.c). */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int64_t s64; typedef uint64_t u64; typedef int32_t s32; typedef uint32_t u32;
typedef uint16_t u16; typedef uint8_t u8; typedef long long loff_t; typedef unsigned int dev_t;
typedef uint16_t __le16; typedef uint32_t __le32; typedef uint64_t __le64; typedef unsigned short umode_t;
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define le64_to_cpu(x) ((u64)(x))
#define S64_MAX INT64_MAX
#define EIO 5
#define EINVAL 22
#define MAX_ERRNO 4095
#define PAGE_SIZE 4096
#define TRACE(...) do {} while (0)
#define ERROR(...) do {} while (0)
#define S_IFMT 00170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#include "squashfs_fs.h"   /* on-disk layouts (unchanged by the series) */
bool nondet_bool(void); int nondet_int(void); u64 nondet_u64(void); u32 nondet_u32(void);

struct squashfs_sb_info { u64 inode_table; u64 directory_table; unsigned int block_size; void *xattr_id_array; };
struct super_block { struct squashfs_sb_info *s_fs_info; };
struct address_space { const void *a_ops; };
struct inode {
	struct super_block *i_sb; loff_t i_size; u64 i_blocks; umode_t i_mode;
	const void *i_op, *i_fop; struct address_space i_data; unsigned int nlink;
};
struct squashfs_inode_info {
	u64 start; int offset; u64 xattr; unsigned int xattr_size; int xattr_count;
	u64 fragment_block; int fragment_size; int fragment_offset; u64 block_list_start;
	u64 dir_idx_start; int dir_idx_offset; int dir_idx_cnt; int parent;
};
struct squashfs_inode_info g_sqi;
static inline struct squashfs_inode_info *squashfs_i(struct inode *i) { return &g_sqi; }
static inline void set_nlink(struct inode *i, unsigned int n) { i->nlink = n; }
static inline void inode_nohighmem(struct inode *i) { }
static inline dev_t new_decode_dev(u32 d) { return d; }
static inline void init_special_inode(struct inode *i, umode_t m, dev_t d) { i->i_mode |= m; }
const int squashfs_aops, squashfs_dir_inode_ops, squashfs_dir_ops, squashfs_inode_ops,
	squashfs_file_operations, squashfs_symlink_inode_ops, squashfs_symlink_aops;

/* helpers.h (patch 2 / 10 of the series) */
static inline u32 squashfs_inode_blk(s64 inode) { return (u32)(inode >> 16); }
static inline u16 squashfs_inode_offset(s64 inode) { return inode & 0xffff; }
static inline s64 squashfs_inode_start(struct squashfs_sb_info *msblk, s64 inode)
{
	s64 start;

	if (inode < 0)
		return -EINVAL;

	if (S64_MAX - msblk->inode_table < squashfs_inode_blk(inode))
		return -EINVAL;

	start = msblk->inode_table + squashfs_inode_blk(inode);

	if (start > msblk->directory_table)
		return -EINVAL;

	return start;
}

/* Contracts of callees (from their sources):
 *  read_metadata: success returns `length` (cache.c: res = length), else -errno
 *  new_inode: 0 or -errno
 *  frag_lookup / xattr_lookup: any value; callers only test < 0          */
static int neg_errno(void) { int e = nondet_int(); __CPROVER_assume(e < 0 && e >= -MAX_ERRNO); return e; }
int squashfs_read_metadata(struct super_block *sb, void *buffer, u64 *block, int *offset, int length)
{
	if (length < 0) return -EIO;
	if (nondet_bool()) return neg_errno();
	if (buffer) {
		__CPROVER_assert(__CPROVER_w_ok(buffer, (size_t)length), "M0 destination large enough");
		unsigned char src[64]; __CPROVER_assume(length <= 64); __builtin_memcpy(buffer, src, (size_t)length);
	}
	*block = nondet_u64(); *offset = nondet_int();
	return length;
}
int squashfs_new_inode(struct super_block *sb, struct inode *inode, struct squashfs_base_inode *b)
{ return nondet_bool() ? 0 : neg_errno(); }
int squashfs_frag_lookup(struct super_block *sb, unsigned int frag, u64 *blk, int *size)
{ *blk = nondet_u64(); *size = nondet_int(); int r = nondet_int(); __CPROVER_assume(r >= -MAX_ERRNO); return r; }
int squashfs_xattr_lookup(struct super_block *sb, unsigned int idx, int *count, unsigned int *size, u64 *xattr)
{ *count = nondet_int(); *size = nondet_u32(); *xattr = nondet_u64(); int r = nondet_int(); __CPROVER_assume(r >= -MAX_ERRNO); return r; }
