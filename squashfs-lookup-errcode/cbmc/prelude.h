/* Kernel environment model for CBMC.  Only types, constants and
 * external functions are modelled; the squashfs functions under test are
 * pasted verbatim from the reconstructed linux-next tree. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef int64_t s64; typedef uint64_t u64; typedef int32_t s32;
typedef uint32_t u32; typedef uint16_t u16; typedef long long loff_t;
typedef uint16_t __le16; typedef uint32_t __le32; typedef uint64_t __le64;
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define le64_to_cpu(x) ((u64)(x))
#define S64_MAX INT64_MAX
#define EIO 5
#define ENOMEM 12
#define EINVAL 22
#define ENAMETOOLONG 36
#define ENOSPC 28
#define ENOSPC 28
#define MAX_ERRNO 4095
#define GFP_KERNEL 0
#define TRACE(...) do {} while (0)
#define ERROR(...) do {} while (0)
#define SQUASHFS_METADATA_SIZE 8192
#define SQUASHFS_NAME_LEN 256
#define SQUASHFS_DIR_COUNT 256
#define IS_ERR(p) ((unsigned long)(const void *)(p) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long e) { return (void *)e; }

bool nondet_bool(void); int nondet_int(void); u64 nondet_u64(void);
u32 nondet_u32(void); unsigned char nondet_uchar(void);

struct squashfs_dir_index { __le32 index; __le32 start_block; __le32 size; unsigned char name[]; };
struct squashfs_dir_entry { __le16 offset; __le16 inode_number; __le16 type; __le16 size; char name[]; };
struct squashfs_dir_header { __le32 count; __le32 start_block; __le32 inode_number; };

struct squashfs_sb_info { u64 directory_table; u64 directory_table_end; };
struct super_block { struct squashfs_sb_info *s_fs_info; };
struct squashfs_inode_info { u64 start; int offset; u64 dir_idx_start; int dir_idx_offset; unsigned int dir_idx_cnt; int parent; };
struct inode { loff_t i_size; struct super_block *i_sb; struct squashfs_inode_info sqi; unsigned long i_ino; };
struct qstr { const unsigned char *name; unsigned int len; };
struct dentry { struct qstr d_name; };
static inline struct squashfs_inode_info *squashfs_i(struct inode *i) { return &i->sqi; }
static inline loff_t i_size_read(const struct inode *i) { return i->i_size; }

/* kmalloc may fail */
static void *kmalloc(size_t n, int f) { return nondet_bool() ? NULL : malloc(n); }
static void kfree(const void *p) { free((void *)p); }

/* Helpers: verbatim from the series (patch 10, 11, 20 of v2). */
static inline s64 squashfs_mkinode(u32 start, u16 offset)
{
	return ((s64)start << 16) | offset;
}
static inline s64 squashfs_dir_start(u32 blk, struct squashfs_sb_info *msblk)
{
	s64 start;

	if (S64_MAX - msblk->directory_table < blk)
		return -EINVAL;

	start = msblk->directory_table + blk;

	if (msblk->directory_table < msblk->directory_table_end &&
			start >= msblk->directory_table_end)
		return -EINVAL;

	return start;
}
static inline bool squashfs_dir_space(struct inode *inode, loff_t length, int size)
{
	loff_t space = i_size_read(inode) - length;

	return space >= size;
}

/* squashfs_read_metadata(): contract taken from fs/squashfs/cache.c.
 * Success returns exactly `length` (res = length) - this is the fact that
 * makes a stale `err` positive.  Failure returns a negative errno. */
int g_bad_block_reads;
unsigned char g_disk[SQUASHFS_NAME_LEN + 16];
static void g_disk_refresh(void) { unsigned char fresh[SQUASHFS_NAME_LEN + 16]; memcpy(g_disk, fresh, sizeof(g_disk)); }
int squashfs_read_metadata(struct super_block *sb, void *buffer,
		u64 *block, int *offset, int length)
{
	if ((s64)*block < 0) g_bad_block_reads++;
	if (length < 0) return -EIO;
	if (nondet_bool()) { int e = nondet_int(); __CPROVER_assume(e < 0 && e >= -MAX_ERRNO); return e; }
	if (buffer) {
		/* the destination must be large enough for the whole read */
		__CPROVER_assert(__CPROVER_w_ok(buffer, (size_t)length), "M0 read_metadata destination is large enough");
		if (length <= 16) { unsigned char src[16]; memcpy(buffer, src, (size_t)length); }
		/* longer reads are names: contents only reach over-approximated comparisons */
	}
	*block = nondet_u64(); __CPROVER_assume((s64)*block >= 0);	/* next_index of a real cache entry */
	*offset = nondet_int(); __CPROVER_assume(*offset >= 0 && *offset < SQUASHFS_METADATA_SIZE);
	return length;
}

/* Comparisons over-approximated (sound: every outcome is explored). */
static int ov_strcmp(const char *a, const char *b) { (void)a; (void)b; return nondet_int(); }
static int ov_strncmp(const char *a, const char *b, size_t n) { (void)a; (void)b; (void)n; return nondet_int(); }
#define strcmp ov_strcmp
#define strncmp ov_strncmp

/* squashfs_iget(): IGET_MODE 0 = contract after the inode.c fix
 * (valid inode or ERR_PTR(-errno)); 1 = what the unfixed inode.c can
 * return (ERR_PTR(32) / ERR_PTR(40), proven in harness_inode.c). */
struct inode g_child;
#ifndef IGET_MODE
#define IGET_MODE 0
#endif
struct inode *squashfs_iget(struct super_block *sb, long long ino, unsigned int n)
{
	int c = nondet_int();
	if (c == 0) return &g_child;
	if (IGET_MODE == 1 && c == 1) return ERR_PTR(nondet_bool() ? 32 : 40);
	int e = nondet_int(); __CPROVER_assume(e < 0 && e >= -MAX_ERRNO);
	return ERR_PTR(e);
}

/* d_splice_alias(): fs/dcache.c returns ERR_CAST(inode) when IS_ERR(inode),
 * otherwise NULL, an alias dentry, or ERR_PTR(-errno). */
struct dentry g_alias;
struct dentry *d_splice_alias(struct inode *inode, struct dentry *d)
{
	if (IS_ERR(inode)) return (struct dentry *)inode;
	/* fs/dcache.c then does security_d_instantiate()/spin_lock(&inode->i_lock) */
	__CPROVER_assert(inode == NULL || inode == &g_child, "P2 inode passed to d_splice_alias is NULL or a real inode");
	int c = nondet_int();
	if (c == 0) return NULL;
	if (c == 1) return &g_alias;
	int e = nondet_int(); __CPROVER_assume(e < 0 && e >= -MAX_ERRNO);
	return ERR_PTR(e);
}
