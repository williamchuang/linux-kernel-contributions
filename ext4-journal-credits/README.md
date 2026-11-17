# ext4: journal credit shortfalls in swap-boot and inline-data writes

**Status:** submitted 2026-11-16, under review.
**Reported by:** syzbot. **Root cause, fixes and reproducers:** William Chuang (Logarchéon Inc.).

- syzbot bug: https://syzkaller.appspot.com/bug?extid=e8180f5590685cbc9037
  ("WARNING in jbd2_journal_dirty_metadata (4)")
- mailing-list thread (lore): https://lore.kernel.org/all/20261117052750.2955-1-founder@logarcheon.com/
- sent as a reply to the syzbot report: https://lore.kernel.org/all/6afabb02.3f8ef711.2cc87e.0003.GAE@google.com/T/

## The bug

One syzbot report covers two independent paths. In both, an ext4 journal
handle dirties a new metadata block after its reserved credits are used up,
so `jbd2_journal_dirty_metadata()` warns, returns `-ENOSPC`, and the journal
is aborted.

| Path | Handle starts with | Blocks dirtied under that handle | Patch |
|---|---|---|---|
| `EXT4_IOC_SWAP_BOOT` (`swap_inode_boot_loader()`) | 2 credits | inode table block, then one quota block per quota type | `0001` |
| Buffered write to an inline-data file with a short user copy | 1 credit | inode table block, then the orphan file block or superblock | `0002` |

## Root cause

**Swap-boot.** Commit `aa507b5faf38` added a quota update
(`dquot_free_space()` / `dquot_alloc_space()`) inside the existing 2-credit
handle. With quota enabled, `ext4_write_dquot()` writes the quota file; its
nested `ext4_journal_start()` reuses the running handle and adds no credits.

**Inline data.** `ext4_generic_write_inline_data()` starts its handle with one
credit, enough for the inode table block. When the copy from user space comes
up short, `ext4_write_inline_data_end()` calls `ext4_orphan_add()` under the
same handle, which dirties a second block. The block write path already
reserves an extra credit for this case in `ext4_write_begin()`. The 1-credit
handle and the orphan add date from inline write support in v3.8.

## The fix

- `0001` — reserve `2 + EXT4_MAXQUOTAS_TRANS_BLOCKS(sb)`, as `ext4_ext_migrate()` does.
  `Fixes: aa507b5faf38 ("ext4: update quota information while swapping boot loader inode")`
- `0002` — start the inline handle with 2 credits.
  `Fixes: f19d5870cbf7 ("ext4: add normal write support for inline data")`

Both carry `Reported-by:` and `Closes:` for the same syzbot report, which
closes only when both are applied.

## Verification

Reproduced before and after the fix on User Mode Linux at mainline
`666539eec`, with standalone reproducers (`repro/`):

| Kernel | Test | jbd2 WARN | ext4 errors | Syscall result | `e2fsck -fn` |
|---|---|---|---|---|---|
| unpatched | swap | 1 | 6 | ioctl returned 0 | exit 4: wrong `i_blocks`, quota inconsistent |
| unpatched | inline, orphan file | 1 | 2 | `pwrite` −1 `ENOSPC` | clean |
| unpatched | inline, superblock orphan list | 1 | 3 | `pwrite` −1 `ENOSPC` | clean |
| unpatched | inline, as uid 65534 | 1 | 2 | `pwrite` −1 `ENOSPC` | clean |
| patched | swap | 0 | 0 | ioctl returned 0 | clean |
| patched | inline, orphan file | 0 | 0 | `pwrite` 10 | clean |
| patched | inline, superblock orphan list | 0 | 0 | `pwrite` 10 | clean |
| patched | inline, as uid 65534 | 0 | 0 | `pwrite` 10 | clean |

Notes on the results:

- On the unpatched kernel the swap-boot ioctl reports success while leaving
  the filesystem inconsistent on disk.
- The inline-data path is reachable without privileges: a user who can write
  to a file on an `inline_data` filesystem can abort its journal.
- The inline-data reproducer mounts with `lazytime`. Without it, the
  timestamp update at the start of `write()` already dirties the inode table
  block in the running transaction, so that block costs no credit and the
  shortfall does not occur.

Not covered: syzbot's own C reproducers, KASAN builds, and `data=journal`.

### Reproduce

```sh
# UML kernel from a mainline tree, with the provided config:
cp repro/uml.config <build>/.config
make ARCH=um O=<build> olddefconfig && make ARCH=um O=<build> -j"$(nproc)" linux

# then, for each test (swap, inline_of, inline_sb, inline_user):
repro/run.sh <build>/linux swap logs/swap.log
```

`run.sh` builds `repro/init.c` statically, creates fresh images with
`mke2fs` (1.47.0 used here), boots UML with `init.c` as PID 1, and runs
`e2fsck -fn` afterwards. Logs of the runs above are in `logs/`.

## Files

| File | Contents |
|---|---|
| `0000-cover-letter.patch` | cover letter as sent |
| `0001-ext4-reserve-quota-credits-in-swap_inode_boot_loader.patch` | swap-boot fix |
| `0002-ext4-reserve-an-orphan-credit-for-inline-data-writes.patch` | inline-data fix |
| `repro/init.c`, `repro/run.sh`, `repro/uml.config` | reproducers, runner, kernel config |
| `logs/` | console logs (`.log`) and `e2fsck` output (`.fsck`), unpatched (`base-*`) and patched (`fix-*`) |
