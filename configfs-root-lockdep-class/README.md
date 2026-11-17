# configfs: give the root directory inode its own lockdep class

**Status:** withdrawn (2026-11-17). Sashiko's review found a real
deadlock that the analysis below missed; see "Correction" just below.
**Reported by:** syzbot. **Analysis, fix and reproducer:** William Chuang (Logarchéon Inc.).

- syzbot bug: https://syzkaller.appspot.com/bug?extid=c75a7db5ca6ae96ed2d4
  ("possible deadlock in configfs_depend_item_unlocked", open since July 2026)
- mailing-list thread (lore): https://lore.kernel.org/all/20261117065314.4309-1-founder@logarcheon.com/T/

## Correction (2026-11-17): a real deadlock

The path check below missed `rename()`. `lock_rename()` locks both
parent directories before `vfs_rename()` finds that configfs has no
`->rename()`, and when one parent is the configfs root it locks the root
first. `mkdir` of `target/usb_gadget/naa.<wwn>/tpgt_1` runs with the
`naa.<wwn>` directory locked by the VFS, and `usbg_make_tpg()` then locks
the root in `configfs_depend_item_unlocked()`. The two orders are
opposite, so the two calls can deadlock.

This was reproduced on unpatched mainline (`666539eec`) in QEMU with
2 CPUs: one process loops `mkdir`/`rmdir` of `tpgt_1`, another loops
`rename()` from the configfs root into `naa.<wwn>`. Both hang within
about a second (`repro/run-stress.sh <bzImage> <log> rename`;
`logs/stress-rename-unpatched.log`). The blocked-task dump shows:

- `mkdir`: `down_write` ← `configfs_depend_item_unlocked` ←
  `usbg_make_tpg` ← `target_fabric_make_tpg` ← `configfs_mkdir` ← `vfs_mkdir`
- `rename`: `down_write_nested` ← `lock_two_directories` ←
  `__start_renaming` ← `filename_renameat2`

The patch would have hidden one link of this from lockdep, so it was
withdrawn and the deadlock reported on the list, in the patch thread
(reply of 2026-11-17 15:04 UTC):
https://lore.kernel.org/all/20261117065314.4309-1-founder@logarcheon.com/T/

The sections below are the original analysis, kept as submitted.

## The report

Lockdep reports a four-lock cycle between the USB gadget and SCSI target
subsystems:

| Link | Lock held | Lock taken | Where |
|---|---|---|---|
| (3) | `tpg_instances_lock` | configfs `i_rwsem` | `usbg_make_tpg()` → `configfs_depend_item_unlocked()` locks the **configfs root** inode |
| (2) | `func_lock` | `tpg_instances_lock` | `tcm_alloc_inst()` |
| (1) | `gadget_subsys.su_mutex` | `func_lock` | `configfs_mkdir()` → `function_make()` |
| (0) | configfs `i_rwsem` | `gadget_subsys.su_mutex` | `configfs_symlink()` holds a **gadget directory** lock; `uvcg_streaming_header_allow_link()` takes `su_mutex` |

## Why it cannot deadlock

The configfs `i_rwsem` in link (0) belongs to a directory under the gadget
subsystem; the one in link (3) belongs to the configfs root. Lockdep sees
one lock because both inodes share the default `i_rwsem` class: only
default groups get their own depth-based classes
(`configfs_set_inode_lock_class()`).

A real deadlock needs some path that holds the **root** inode lock and then
takes a subsystem's `su_mutex`. Every path that takes the root lock was
checked:

| Path that locks the configfs root | Takes `su_mutex` while holding it? |
|---|---|
| `mkdir`, `rmdir`, `symlink`, `unlink`, `rename` in the root | No: the root has only `->lookup` and `->setattr`, so the VFS fails these before configfs runs |
| `configfs_lookup()` | No |
| `configfs_setattr()` (e.g. `chmod`) | No |
| `configfs_readdir()` | No |
| `configfs_dir_open()`, `configfs_dir_close()` | No |
| `configfs_register_subsystem()`, `configfs_unregister_subsystem()` | No |
| `configfs_depend_item()`, `configfs_depend_item_unlocked()` | No |

## The fix

Give the root inode's `i_rwsem` its own `lock_class_key` in
`configfs_fill_super()`. Lockdep then keeps the root lock apart from
directory locks. Nothing changes without `CONFIG_LOCKDEP`.

- `Fixes: 4bb8548df632 ("usb: gadget: f_tcm: add configfs support")`
- `Reported-by:` / `Closes:` for syzbot `c75a7db5ca6ae96ed2d4`

Earlier work on the same family: Yun Zhou's merged fix removed the
`dep_lock` variant of this cycle from `usbg_make_tpg()`; the
`tpg_instances_lock` variant reported here remained.

## Verification

QEMU x86_64 (no KVM), `CONFIG_PROVE_LOCKING=y`, mainline `666539eec`. The
initramfs program `repro/init.c` runs the three operations behind the
cycle, then takes the root lock in the other ways userspace can, then
tears everything down:

| Step | Unpatched | Patched |
|---|---|---|
| A: symlink in a UVC streaming header (link 0) | ok | ok |
| B: `mkdir functions/tcm.0` (links 1, 2) | ok | ok |
| C: `mkdir target/usb_gadget/naa.*/tpgt_1` (link 3) | ok, **lockdep reports the cycle** | ok, no report |
| `readdir`, `chmod`, `mkdir` on the configfs root | ok, ok, `EPERM` | ok, ok, `EPERM` |
| teardown (`rmdir`, `unlink`, undepend paths, `umount`) | ok | ok |

The lockdep report on the unpatched kernel has the same four links, in the
same order, as syzbot's report. The patch also builds without warnings with
`W=1` and without `CONFIG_LOCKDEP`.

### Further checks

| Check | Unpatched | Patched |
|---|---|---|
| configfs kselftest (`tools/testing/selftests/filesystems/configfs`, 25 tests, including its symlink-versus-rmdir race test), with lockdep on | 25/25 pass, no lockdep report | 25/25 pass, no lockdep report |
| 60 s concurrency stress (`repro/stress.c`): `tpgt_1` mkdir/rmdir, UVC symlink/unlink, `readdir` + `chmod` of the root, in three processes | no stall; lockdep reports the cycle | no stall; no report |
| `fs/configfs/mount.o` without `CONFIG_LOCKDEP` | — | disassembles identically to the unpatched object |
| `W=1` build | — | no warnings |

The stress run finding no stall on the unpatched kernel agrees with the
analysis above: the reported cycle is not a real deadlock. The identical
object code shows the patch changes nothing in kernels built without
lockdep.

For the kselftest, the sample module (`CONFIG_SAMPLE_CONFIGFS=m`) is loaded
in the initramfs by busybox `modprobe`; `repro/kselftest-init.sh` is the
init script used.

Not covered: syzbot's own C reproducer.

### Reproduce

```sh
# x86_64 kernel from a mainline tree, with the provided config:
cp repro/qemu-x86_64.config <build>/.config
make O=<build> olddefconfig && make O=<build> -j"$(nproc)" bzImage

repro/run.sh <build>/arch/x86/boot/bzImage logs/run.log
```

`run.sh` builds `repro/init.c` statically into an initramfs and boots it
with `qemu-system-x86_64`. Logs of the runs above are in `logs/`.

## Files

| File | Contents |
|---|---|
| `0001-configfs-give-the-root-directory-inode-its-own-lockd.patch` | the patch as sent |
| `repro/init.c`, `repro/run.sh`, `repro/qemu-x86_64.config` | reproducer, runner, kernel config |
| `repro/stress.c`, `repro/run-stress.sh` | concurrency stress test and runner |
| `repro/kselftest-init.sh` | initramfs init used for the configfs kselftest |
| `logs/unpatched.log`, `logs/patched.log` | QEMU console logs of the reproducer |
| `logs/kselftest-*.log`, `logs/stress-*.log` | kselftest and stress runs, unpatched and patched |
| `logs/stress-rename-unpatched.log` | the rename-versus-mkdir deadlock on unpatched mainline, with the blocked-task dump |
