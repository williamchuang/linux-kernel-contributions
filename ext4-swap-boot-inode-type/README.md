# ext4: reject a non-regular boot loader inode in EXT4_IOC_SWAP_BOOT

**Status:** submitted 2026-11-16, under review.
**Found by:** Sashiko AI review, while reviewing the [ext4 journal credit series](../ext4-journal-credits/).
**Fix and reproducer:** William Chuang (Logarchéon Inc.), following the check Theodore Ts'o described in 2023.

- mailing-list thread (lore): https://lore.kernel.org/all/?q=s%3A%22reject+a+non-regular+boot+loader+inode%22
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

- `Fixes: 991ed014de08 ("ext4: fix bug_on in __es_tree_search caused by bad boot loader inode")`
- `Suggested-by: Theodore Ts'o`

## Verification

Run on User Mode Linux at mainline `666539eec`. Inode 5 is set with
`debugfs`, then `EXT4_IOC_SWAP_BOOT` is issued twice:

| Kernel | Inode 5 before | Warning | Result of both swaps | Inode 5 on disk after | `e2fsck -fn` |
|---|---|---|---|---|---|
| unpatched | unused (new filesystem) | none | succeed | regular | clean |
| unpatched | directory | `lock_two_nondirectories` | succeed | regular (type changed) | clean |
| unpatched | character device | none | succeed | regular (type changed) | clean |
| patched | unused (new filesystem) | none | succeed | regular | clean |
| patched | directory | none | `EFSCORRUPTED`, error logged | directory (unchanged) | reports the invalid inode 5, left for repair |
| patched | character device | none | `EFSCORRUPTED`, error logged | character device (unchanged) | clean |

For the directory case, `e2fsck -fn` on the patched run reports the
corruption the test injected ("Reserved inode 5 ... has invalid mode"),
since the kernel now refuses to touch it. On the unpatched runs it reports
nothing, because the kernel has already rewritten inode 5 as a regular file.
`e2fsck` does not flag a character-device inode 5.

Not covered: KASAN builds and syzbot's 2023 reproducer.

### Reproduce

```sh
# UML kernel from a mainline tree, with the provided config:
cp repro/uml.config <build>/.config
make ARCH=um O=<build> olddefconfig && make ARCH=um O=<build> -j"$(nproc)" linux

# then, for each case (fresh, dir, chrdev):
repro/run.sh <build>/linux dir logs/dir.log
```

`run.sh` builds `repro/init.c` statically, creates a fresh image with
`mke2fs` (1.47.0 used here), sets the mode of inode 5 with `debugfs`, boots
UML with `init.c` as PID 1, and runs `e2fsck -fn` afterwards. Logs of the
runs above are in `logs/`.

## Files

| File | Contents |
|---|---|
| `0001-ext4-reject-a-non-regular-boot-loader-inode-in-EXT4_.patch` | the patch as sent |
| `repro/init.c`, `repro/run.sh`, `repro/uml.config` | reproducer, runner, kernel config |
| `logs/` | console logs (`.log`) and `e2fsck` output (`.fsck`), unpatched (`base-*`) and patched (`fix-*`) |
