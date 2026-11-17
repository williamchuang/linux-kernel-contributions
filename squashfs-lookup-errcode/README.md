# squashfs: error code missing on the directory header bounds check in lookup

**Status:** submitted 2026-11-16, under review.
**Found by:** William Chuang (Logarchéon Inc.), by model checking the code queued for merge.

- mailing-list thread (lore): https://lore.kernel.org/all/20261116233530.47177-1-founder@logarcheon.com/
- sent as a reply to the kernel test robot report: https://lore.kernel.org/r/202611131955.0Gd7ipSP-lkp@intel.com/
- related syzbot reports on the same code (linux-next `84abb955b101`):
  [`d393ed2b27449be4992b`](https://syzkaller.appspot.com/bug?extid=d393ed2b27449be4992b) (`step_into_slowpath`),
  [`03fdf84c0915fd48f361`](https://syzkaller.appspot.com/bug?extid=03fdf84c0915fd48f361) (`lookup_one_qstr_excl`),
  [`0b7a8fb8279a59e8a188`](https://syzkaller.appspot.com/bug?extid=0b7a8fb8279a59e8a188) (`lookup_open`),
  [`5d7ba5f6127ed887f459`](https://syzkaller.appspot.com/bug?extid=5d7ba5f6127ed887f459) (`__d_obtain_alias`)

## The bug

Phillip Lougher's 22-patch SquashFS hardening series (v2, 2026-11-11) added
three `squashfs_dir_space()` bounds checks to `squashfs_lookup()`, one before
each of the directory header, entry and name reads. All three jump to
`read_failure` without setting `err`, so `squashfs_lookup()` returns
`ERR_PTR(err)` with `err` either the positive byte count of the previous
successful metadata read, or uninitialised on the first iteration. `IS_ERR()`
does not catch a positive value, and the VFS dereferences it as a dentry.

## How the fix landed

| Change | By | Covers |
|---|---|---|
| `[PATCH 1/2]` and `[PATCH 2/2]` (2026-11-14) | Aibing Chen | all three `namei.c` checks; two `inode.c` checks with the same defect |
| `squashfs-fix-variables-in-directory-code-and-add-more-sanity-checks-fix` (2026-11-16) | Andrew Morton, after a smatch report by Dan Carpenter | the **entry** and **name** checks only |
| `squashfs-sanity-check-directory-metadata-block-location-fix` (2026-11-16) | Andrew Morton | a separate dead `u64 block < 0` check (also found here) |
| **this patch** | William Chuang | the **header** check, missed by Andrew's fix |

Andrew's fix is the version in the merge queue, so the header path was still
open. Model checking his exact code produces two counterexamples: a 15-byte
directory returns `ERR_PTR(12)`, and a 9-byte directory returns
`ERR_PTR(<uninitialised>)`. The patch changes one line (`goto read_failure` →
`goto data_error`, i.e. `-EIO`). Chen's PATCH 2/2 contained the same hunk; the
commit message credits it. Chen's PATCH 1/2 (`inode.c`) is still required.

- `Fixes: 5213b0a08263 ("Squashfs: fix variables in directory code and add more sanity checks")`

## Verification

CBMC (bit-precise bounded model checking) on the functions extracted verbatim
from the kernel source. Details, method and every counterexample are in
[`VERIFICATION.md`](VERIFICATION.md).

1. **Source reconstruction.** `fs/squashfs` at linux-next `84abb955b101` was
   rebuilt from mainline plus the series, and every file was checked against
   the git blob hashes in the patches' own `index` lines.
2. **Bounded proofs.** Lookup, `readdir` and `squashfs_read_inode` with every
   loop completely unwound (unwinding assertions pass).
3. **Inductive proofs (no size bound).** One loop iteration from an arbitrary
   state satisfying the invariant, so the result holds for every directory
   size below 2^32 and any index length; non-vacuity was checked.
4. **Callee contracts proven on real source.** `squashfs_read_metadata()`
   returns exactly `length` or `-errno`; `squashfs_iget()` returns a real
   inode or `ERR_PTR(-errno)`.
5. **Sweep of the series.** All 69 error exits the 22 patches add were
   inspected; Chen's five sites are the only stale-error ones.

| Tree | Result |
|---|---|
| linux-next `84abb955b101` (unfixed) | lookup and `squashfs_read_inode` **fail** |
| + Andrew Morton's two queued fixes | lookup still **fails** (header path) |
| + this patch + Chen PATCH 1/2 | **all properties pass** (lookup, `readdir`, `squashfs_read_inode`, contracts) |

Properties checked: lookup returns NULL, a dentry, or `ERR_PTR(-errno)`; the
inode handed to `d_splice_alias()` is NULL or real; `squashfs_read_inode()`
returns 0 or `-errno`; every directory read stays inside `i_size`; no rejected
directory location is read; array-bounds, pointer and signed-overflow safety.

Not covered: a run of the syzbot C reproducers on a built kernel, and the
cache, decompressor and block-I/O layers below `squashfs_read_metadata()`,
which are modelled by contract.

```sh
./run_all.sh     # unfixed linux-next and Chen's patches   (~7 min)
./run_final.sh   # Andrew's queue, and the final tree     (~6 min)
```

Needs git, python3, gcc and cbmc ≥ 5.95. Outputs go to `results/`.

## Files

| File | What it is |
|---|---|
| `0001-squashfs-set-an-error-code-on-the-directory-header-b.patch` | the submitted patch |
| `VERIFICATION.md` | complete verification report |
| `cbmc/` | CBMC harnesses and environment models |
| `reconstruction/` | series hunks, Andrew's and Chen's patches, rebuilt source files, `rebuild.sh` |
| `results/` | CBMC output for every run in the report |
| `run_all.sh`, `run_final.sh` | reproduce every result from a clean clone |
| `README.md` | this file |
