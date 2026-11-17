#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint64_t u64; typedef int64_t s64;
#define EIO 5
#define ENOMEM 12
#define MAX_ERRNO 4095
#define SQUASHFS_METADATA_SIZE 8192
#define unlikely(x) (x)
#define TRACE(...) do {} while (0)
#define IS_ERR(p) ((unsigned long)(const void *)(p) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long e) { return (void *)e; }
bool nondet_bool(void); int nondet_int(void); u64 nondet_u64(void); unsigned nondet_unsigned(void);
struct squashfs_cache_entry { int error; int length; u64 next_index; };
struct squashfs_sb_info { void *block_cache; };
struct super_block { struct squashfs_sb_info *s_fs_info; };
/* squashfs_cache_get(): entry->error is 0 or -errno; a good entry has 1..8192 bytes */
struct squashfs_cache_entry g_e;
struct squashfs_cache_entry *squashfs_cache_get(struct super_block *sb, void *c, u64 b, int l)
{
	g_e.error = nondet_int(); __CPROVER_assume(g_e.error <= 0 && g_e.error >= -MAX_ERRNO);
	g_e.length = nondet_int(); __CPROVER_assume(g_e.length >= 1 && g_e.length <= SQUASHFS_METADATA_SIZE);
	g_e.next_index = nondet_u64();
	return &g_e;
}
void squashfs_cache_put(struct squashfs_cache_entry *e) { }
/* squashfs_copy_data(): copies min(length, entry->length - offset) bytes (cache.c) */
int squashfs_copy_data(void *buf, struct squashfs_cache_entry *e, int off, int len)
{
	int rem = e->length - off;
	return len < rem ? len : rem;
}
#include "rm.c"

/* --- squashfs_iget --- */
#define I_NEW 1
struct inode { int state; };
struct inode g_ino;
struct inode *iget_locked(struct super_block *sb, unsigned long n) { return nondet_bool() ? NULL : &g_ino; }
unsigned inode_state_read_once(struct inode *i) { return nondet_unsigned(); }
/* P3 (proven in C_read_inode): returns 0 or -errno */
int squashfs_read_inode(struct inode *i, long long ino) { int r = nondet_int(); __CPROVER_assume(r == 0 || (r < 0 && r >= -MAX_ERRNO)); return r; }
void iget_failed(struct inode *i) { }
void unlock_new_inode(struct inode *i) { }
#include "ig.c"

int main(void)
{
	struct squashfs_sb_info sbi; struct super_block sb = { &sbi };
	u64 block = nondet_u64(); int offset = nondet_int(); int length = nondet_int();
	__CPROVER_assume(length <= 260);	/* loop is unrolled; see note */
	static unsigned char buf[260];
	int r = squashfs_read_metadata(&sb, nondet_bool() ? buf : NULL, &block, &offset, length);
	__CPROVER_assert(r == length || (r < 0 && r >= -MAX_ERRNO),
		"C1 squashfs_read_metadata returns exactly length, or -errno");
	__CPROVER_assert(r < 0 || (offset >= 0 && offset < SQUASHFS_METADATA_SIZE),
		"C2 on success, *offset stays in a metadata block");

	struct inode *ip = squashfs_iget(&sb, (long long)nondet_u64(), nondet_unsigned());
	__CPROVER_assert(ip == &g_ino || IS_ERR(ip), "C3 squashfs_iget returns a real inode or ERR_PTR(-errno)");
	return 0;
}
