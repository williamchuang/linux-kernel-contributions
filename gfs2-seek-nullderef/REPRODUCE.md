# Reproducing syzbot `gfs2_glock_dq` GPF locally

Bug: **general protection fault in gfs2_glock_dq (2)** —
`syzbot+5a5f492ccae698fd7434@syzkaller.appspotmail.com`
Dashboard: https://syzkaller.appspot.com/bug?extid=5a5f492ccae698fd7434

The crash trace matches the fix exactly: the filesystem is withdrawn,
`gfs2_glock_nq` fails, the holder's `gh_gl` is left NULL, and
`gfs2_seek_data` → `gfs2_glock_dq_uninit` → `gfs2_glock_dq` faults taking
`gh->gh_gl->gl_lockref.lock` (KASAN: null-ptr-deref in range
`[0x20-0x27]`, i.e. the `gl_lockref.lock` offset).

## Artifacts to download (from the 2026/06/20 crash with a C reproducer)

| Artifact | URL |
|---|---|
| C reproducer | https://syzkaller.appspot.com/text?tag=ReproC&x=10e34d56580000 |
| syz reproducer | https://syzkaller.appspot.com/text?tag=ReproSyz&x=1391e566580000 |
| kernel `.config` (KASAN) | https://syzkaller.appspot.com/text?tag=KernelConfig&x=f5fece1a9477c321 |
| crash commit | `a975094bf98ca97be9146f9d3b5681a6f9cf5ce3` |
| prebuilt bzImage / vmlinux | linked under "Assets" on the dashboard row |

The C reproducer mounts a crafted (corrupt) gfs2 image over a loop device with
`errors=withdraw`, then does an `lseek(SEEK_DATA/SEEK_HOLE)` on a file in it.
The corruption forces a withdraw, which is the path that leaves the glock
holder NULL.

## Requirements

The reproducer mounts filesystems, sets up loop devices, and runs as root
against a custom KASAN kernel, so it needs a machine with KVM and a throwaway
VM. Never run a syzbot reproducer on a host system.

## Steps (local machine with KVM)

1. **Trees.** To *see* the crash, build at the crash commit (or any pre-fix
   upstream); to *confirm the fix*, apply the v2 patch on top (or build a tree
   that already carries it):

       git checkout a975094bf98c            # reproduce the crash
       # or, to confirm the fix:
       git checkout a975094bf98c
       git am /path/to/v2-0001-gfs2-seek-fix.patch

2. **Config.** Use the syzbot `.config` (it already enables `KASAN`, `GFS2_FS`,
   loop, and the debug locking that turns the NULL deref into a clean report):

       cp downloaded.config .config
       make olddefconfig
       make -j"$(nproc)" bzImage

3. **Root image.** Build a minimal Debian image (one-time), e.g. with
   syzkaller's helper:

       # from a syzkaller checkout
       ./tools/create-image.sh            # produces bullseye.img + a key

4. **Boot in QEMU** (KVM, with the console on stdio):

       qemu-system-x86_64 -enable-kvm -m 2G -smp 2 \
         -kernel arch/x86/boot/bzImage \
         -append "console=ttyS0 root=/dev/sda earlyprintk=serial net.ifnames=0" \
         -drive file=bullseye.img,format=raw -nographic \
         -net user,host=10.0.2.10 -net nic

5. **Build and run the reproducer inside the VM**, as root:

       gcc -O2 -pthread -o repro repro.c -static
       ./repro

6. **Expected outcome.**
   - Unpatched kernel: within a few iterations, a GPF / KASAN null-ptr-deref in
     `gfs2_glock_dq`, with `gfs2_seek_data`/`gfs2_llseek` in the call trace —
     the reported crash.
   - Patched kernel (v2 applied): no crash; the `lseek` on the withdrawn
     filesystem returns an error cleanly and the reproducer loops without a
     fault.

## Note

This is independent confirmation only. The fix has been reviewed by the gfs2
maintainer (Andreas Gruenbacher), who supplied its final form, and the `Reported-by:`/`Closes:`
tags let syzbot auto-close the bug once the fix lands in a tree it tracks, so
running this is optional — it is the check that `#syz test` would have
performed automatically had syzbot's test bot not been disabled.
