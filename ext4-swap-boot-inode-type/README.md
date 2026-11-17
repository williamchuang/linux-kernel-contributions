# ext4: reject a non-regular boot loader inode in EXT4_IOC_SWAP_BOOT

**Status:** v3 submitted 2026-11-17, under review; Sashiko reviewed v3 and found no issues (v1 and v2 were submitted 2026-11-16/17).
**Found by:** Sashiko AI review, while reviewing the [ext4 journal credit series](../ext4-journal-credits/).
**Fix and reproducer:** William Chuang (Logarchéon Inc.), following the check Theodore Ts'o described in 2023.

- mailing-list thread (lore), v1: https://lore.kernel.org/all/20261117061822.3826-1-founder@logarcheon.com/
  (v2 and v3 are sent as replies to it)
- v3: https://lore.kernel.org/all/20261117223350.6920-1-founder@logarcheon.com/
- Sashiko review of v3 (no issues): https://sashiko.dev/#/patchset/20261117223350.6920-1-founder@logarcheon.com?part=1
- Sashiko report: https://sashiko.dev/#/bug/linux-8cfc8e22-8259-4c89-8a8b-64c16e4481a5
- 2023 discussion, with the suggested check:
  https://lore.kernel.org/all/tencent_DABB2333139E8D1BCF4B5D1B2725FABA9108@qq.com/T/

## The bug

`EXT4_IOC_SWAP_BOOT` swaps a file's data with the boot loader inode
(inode 5). On a new filesystem inode 5 is unused: its mode is 0, and
`ext4_iget()` returns it as a bad inode, which `make_bad_inode()` sets to
`S_IFREG`. On a corrupted or crafted filesystem, `ext4_iget()` can instead
return inode 5 as a directory or another non-regular type, set up with that
type's inode and file operations.

`swap_inode_boot_loader()` then:

1. passes it to `lock_two_nondirectories()`, which warns if it is a
   directory, and
2. in its "never used as a boot loader" branch, sets `i_mode` to `S_IFREG`,
   changing the type of a live in-core inode while it keeps the operations
   of its old type.

## Root cause

Commit `991ed014de08` ("ext4: fix bug_on in __es_tree_search caused by bad
boot loader inode", 2022) made that branch re-initialise any inode 5 whose
mode is not `S_IFREG`, not only a bad one. A non-regular inode 5 is
therefore converted in place rather than rejected.

## The fix

Right after `ext4_iget()`, report the corruption with `EXT4_ERROR_INODE()`
and return `-EFSCORRUPTED` unless the inode is a regular file. The unused
case is unaffected, because `make_bad_inode()` has already made it
`S_IFREG`.

v2 also marks the rejected inode bad before `iput()`, as
`ext4_get_journal_inode()` does. Sashiko's review of v1 pointed out that
otherwise a corrupted inode 5 with `i_links_count` 0 reaches the deletion
path of `ext4_evict_inode()`; the v1 run below confirms it
(`ext4_free_inode: reserved or nonexistent inode 5`).

v3 keeps the v1/v2 check but changes how the rejected inode is released.
Sashiko's review of v2 pointed out that `make_bad_inode()` changes the
type and operations of an inode that other callers may already hold
in the inode cache, so a concurrent caller could pass the check. v3
leaves the inode's type alone, so every caller rejects it, and instead
sets `i_nlink` to 1 under the inode lock when it is 0, so the last
`iput()` does not take the deletion path for the reserved inode. The
later `S_ISREG` test in the "never used as a boot loader" branch is now
redundant and is dropped.

- `Fixes: 991ed014de08 ("ext4: fix bug_on in __es_tree_search caused by bad boot loader inode")`
- `Suggested-by: Theodore Ts'o`

## Verification

Run on User Mode Linux at mainline `666539eec`. Inode 5 is set with
`debugfs`, then `EXT4_IOC_SWAP_BOOT` is issued twice:

| Kernel | Inode 5 before | Warning | Result of both swaps | Inode 5 on disk after | Other errors |
|---|---|---|---|---|---|
| unpatched | unused (new filesystem) | none | succeed | regular | none |
| unpatched | directory | `lock_two_nondirectories` | succeed | regular (type changed) | none |
| unpatched | directory, `i_links_count` 0 | `lock_two_nondirectories` | succeed | regular (type changed) | none |
| unpatched | character device | none | succeed | regular (type changed) | none |
| v1 | directory, `i_links_count` 0 | none | `EFSCORRUPTED` | directory (unchanged) | `ext4_free_inode: reserved or nonexistent inode 5` |
| v2 | unused (new filesystem) | none | succeed | regular | none |
| v2 | directory | none | `EFSCORRUPTED`, error logged | directory (unchanged) | none |
| v2 | directory, `i_links_count` 0 | none | `EFSCORRUPTED`, error logged | directory (unchanged) | none |
| v2 | character device | none | `EFSCORRUPTED`, error logged | character device (unchanged) | none |
| v3 | unused (new filesystem) | none | succeed | regular | none |
| v3 | directory | none | `EFSCORRUPTED`, error logged | directory (unchanged) | none |
| v3 | directory, `i_links_count` 0 | none | `EFSCORRUPTED`, error logged | directory (unchanged) | none |
| v3 | character device | none | `EFSCORRUPTED`, error logged | character device (unchanged) | none |
| v3 | directory, `i_links_count` 0; two processes, 500 swaps each | none | 0 of 1000 succeed | directory (unchanged) | none |

For the directory cases, `e2fsck -fn` on the patched runs reports the
corruption the test injected ("Reserved inode 5 ... has invalid mode"),
since the kernel now refuses to touch it. On the unpatched runs it reports
nothing, because the kernel has already rewritten inode 5 as a regular file.
`e2fsck` does not flag a character-device inode 5.

The two-process run (`race`) is on a single-CPU UML guest, so it
exercises interleaving but not true parallel execution.

Not covered: KASAN builds, SMP runs and syzbot's 2023 reproducer.

### Reproduce

```sh
# UML kernel from a mainline tree, with the provided config:
cp repro/uml.config <build>/.config
make ARCH=um O=<build> olddefconfig && make ARCH=um O=<build> -j"$(nproc)" linux

# then, for each case (fresh, dir, dir0, chrdev, race):
repro/run.sh <build>/linux dir logs/dir.log
```

`run.sh` builds `repro/init.c` statically, creates a fresh image with
`mke2fs` (1.47.0 used here), sets the mode of inode 5 with `debugfs`, boots
UML with `init.c` as PID 1, and runs `e2fsck -fn` afterwards. Logs of the
runs above are in `logs/`.

## Files

| File | Contents |
|---|---|
| `v3-0001-ext4-reject-a-non-regular-boot-loader-inode-in-EX.patch` | v3 as sent |
| `v2-0001-ext4-reject-a-non-regular-boot-loader-inode-in-EX.patch` | v2 as sent |
| `0001-ext4-reject-a-non-regular-boot-loader-inode-in-EXT4_.patch` | v1 as sent |
| `repro/init.c`, `repro/run.sh`, `repro/uml.config` | reproducer, runner, kernel config |
| `logs/` | console logs (`.log`) and `e2fsck` output (`.fsck`): unpatched (`base-*`), v3 (`v3/fix-*`), v2 (`fix-*`), v1 (`v1/fix-*`) |
