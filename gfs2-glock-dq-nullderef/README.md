# GFS2: NULL pointer dereference in `gfs2_glock_dq` during `lseek()`

**Status:** submitted, v2 under review.
**Reported by:** syzbot. **Root-cause + v1:** William Chuang (Logarchéon Inc.).
**Final form of the fix:** GFS2 maintainer Andreas Gruenbacher (`Suggested-by:`).

- syzbot bug: https://syzkaller.appspot.com/bug?extid=5a5f492ccae698fd7434
- mailing-list thread (lore): https://lore.kernel.org/all/20261116180002.44336-1-founder@logarcheon.com/T/

## Terms

- **GFS2** (Global File System 2): a shared-disk cluster filesystem in the Linux kernel.
- **`lseek()`**: the system call that moves a file's read/write position.
- **`SEEK_DATA` / `SEEK_HOLE`**: `lseek()` options that find the next region of a
  sparse file that holds data, or the next unallocated region.
- **glock**: GFS2's cluster-wide lock; `gfs2_glock_dq()` releases one.

## The bug

A general protection fault in `gfs2_glock_dq`, reachable from userspace by an
`lseek()` data search (`SEEK_DATA` and its counterpart mode) on a file in a GFS2
(Global File System 2) filesystem that has been withdrawn.
KASAN reports a null-ptr-deref in range `[0x20-0x27]` — the `gl_lockref.lock`
offset inside `struct gfs2_glock`.

## Root cause

When the filesystem has been withdrawn, `gfs2_glock_nq()` returns an error
without queuing the holder, and `gfs2_glock_nq_init()` then calls
`gfs2_holder_uninit()`, which sets `gh.gh_gl` to NULL. The two `lseek()` search
helpers in `fs/gfs2/inode.c` (`gfs2_seek_data()` and its counterpart) called
`gfs2_glock_dq_uninit()` regardless of the outcome, so
`gfs2_glock_dq()` dereferenced the NULL glock when it took
`gl->gl_lockref.lock` — the general protection fault.

## The fix

Only call `gfs2_glock_dq_uninit()` when `gfs2_glock_nq_init()` succeeded, using
a `goto out` to skip the dequeue on the failure path.

- `Fixes: 3a27411cb4bc ("gfs2: Implement SEEK_HOLE / SEEK_DATA via iomap")`
- `Cc: stable@vger.kernel.org`

## Verification (3 methods, proportionate to a 12-line fix)

1. **Deterministic root-cause analysis.** The KASAN fault offset (`0x20`)
   matches `gl_lockref.lock` in `struct gfs2_glock`, and the call trace
   (`gfs2_seek_data` → `gfs2_glock_dq_uninit` → `gfs2_glock_dq`) pins the NULL
   glock to the withdraw path. The fix follows the idiom already used elsewhere
   in the same file (`gfs2_fiemap`, `gfs2_getattr`).

2. **Machine-checked Coq proof** (`GlockHolder.v`). A model of the control-flow
   property the fix establishes, checked by `coqc` (Coq 8.18):
   - `fixed_is_safe` — the `goto out` flow dequeues only an *acquired* holder,
     so `gh_gl` is never NULL at `gfs2_glock_dq_uninit`;
   - `buggy_is_unsafe` — the old flow dequeues an uninitialised holder on the
     failure path, i.e. the reported NULL dereference.

   ```sh
   coqc GlockHolder.v       # exit 0 = both theorems accepted
   ```

3. **syzbot C reproducer** (`REPRODUCE.md`). Runtime confirmation: the crash
   appears on an unpatched KASAN kernel and is gone with the patch applied. The
   reproducer and kernel config are linked from the syzbot bug page; steps to
   build and run them in a VM are in `REPRODUCE.md`. (Running this is optional —
   the maintainer has reviewed the fix and supplied its final form.)

## Files

| File | What it is |
|---|---|
| `v2-0001-gfs2-glock-dq-nullderef-fix.patch` | the submitted patch (maintainer's final form, carried forward under the author's Signed-off-by) |
| `GlockHolder.v` | the machine-checked Coq proof of the holder-lifecycle property |
| `REPRODUCE.md` | download links and VM steps for the syzbot reproducer |
| `README.md` | this file |
