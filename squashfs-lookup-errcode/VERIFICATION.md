# SquashFS lookup crashes in linux-next 84abb955b101: formal verification of the proposed fixes

Prepared by William Chuang (Logarchéon Inc.), 2026-11-16.

## Scope

| syzbot extid | crash site | path |
|---|---|---|
| `d393ed2b27449be4992b` | `step_into_slowpath()` | path walk → `squashfs_lookup()` |
| `03fdf84c0915fd48f361` | `lookup_one_qstr_excl()` | → `squashfs_lookup()` |
| `0b7a8fb8279a59e8a188` | `lookup_open()` | → `squashfs_lookup()` |
| `5d7ba5f6127ed887f459` | `__d_obtain_alias()` | `squashfs_export_iget()` → `squashfs_iget()` |

Introducing commit: `5213b0a08263` = Phillip Lougher, "[PATCH v2 20/22] Squashfs: fix variables in
directory code and add more sanity checks" (posted 2026-11-11).
Fixes under test: Aibing Chen, 2026-11-14,
"[PATCH 1/2] squashfs: return -EINVAL on invalid directory inode size" and
"[PATCH 2/2] squashfs: return -EIO on out-of-bounds directory entry in lookup".

## 1. Source reconstruction (the verification runs on the real code)

kernel.org was not reachable from the verification machine, so `fs/squashfs` at linux-next
`84abb955b101` was rebuilt from mainline `141b164bbe18` plus the hunks of the v2 series, and every
step was checked against the git blob hashes in the patches' own `index` lines:

| file | result | evidence |
|---|---|---|
| `dir.c` | identical | `33b8225fcdaa` (patch 20 post-image) |
| `inode.c` | identical | every intermediate hash of patches 2, 7, 10, 12, 15, 19, 20 matches; final `5404d7ba6fc0` |
| `namei.c` | identical to linux-next | `5f8decee2`, the pre-image hash in Chen's PATCH 2/2, which was generated against linux-next |

Note: patch 20's own `index` line for `namei.c` says `89d5a09544dc`; that hash is produced only if one
line reads `if(max_index <` instead of the posted `if (max_index <`, so the author's tree differed from
the posted text by one space. No semantic difference.

Chen's patches apply cleanly and reproduce their stated post-image hashes (`4cf8a41b5`, `5dc21ddaf`).

## 2. Method

CBMC 5.95.1 (bit-precise bounded model checking), run on the functions extracted verbatim from the
reconstructed files. Only the environment is modelled; every model is a sound over-approximation
taken from the callee's source:

* `squashfs_read_metadata()` — success returns exactly `length` (cache.c: `res = length`), failure a
  `-errno`; all on-disk bytes are unconstrained.
* `squashfs_iget()` — mode 0: valid inode or `ERR_PTR(-errno)`; mode 1: may also return
  `ERR_PTR(32)` / `ERR_PTR(40)` (what unfixed `inode.c` produces; proven in C below).
* `d_splice_alias()` — returns `ERR_CAST(inode)` for error pointers, otherwise dereferences `inode`.
* name comparisons (`strcmp`, `strncmp`) return arbitrary results.
* mount-time invariants from `super.c`: `inode_table < directory_table <= bytes_used`, directory
  `i_size >= 3` (patch 20).

Properties:

* **P1** `squashfs_lookup()` returns NULL, a real dentry, or `ERR_PTR(-errno)`.
* **P2** the inode handed to `d_splice_alias()` is NULL or real.
* **P3** `squashfs_read_inode()` returns 0 or `-errno`.
* **P4** `squashfs_readdir()` returns 0.
* **P5** no metadata read uses a directory location rejected by `squashfs_dir_start()`.
* **R1–R3** every directory read stays inside `i_size` (the purpose of `squashfs_dir_space()`).
* plus CBMC's array-bounds, pointer and signed-overflow checks on all code.

Two verification modes for lookup:

* **A — bounded:** `i_size <= 40`, up to 2 index entries, all loops completely unwound
  (unwinding assertions pass, so no path within the bound is cut).
* **B — inductive (unbounded):** each loop iteration starts from an arbitrary state satisfying the
  invariant `length >= 0, inode == NULL`; one iteration is checked. Every real iteration, for any
  `i_size < 2^32` and any number of index entries, is an instance of this. Non-vacuity was checked:
  the `-EIO`, found-entry and loop-exit paths are all reachable.

CBMC loop contracts (`--apply-loop-contracts`) were tried first and rejected: that encoding drops
paths that `goto` out of a loop, so it "proved" the unfixed code. It is not used for any result.

## 3. Results

| run | unfixed linux-next | + Chen 1/2 + 2/2 |
|---|---|---|
| A lookup, iget mode 0 | **P1 FAILS** | P1–P2, R1–R3, memory safety: pass |
| A lookup, iget mode 1 (unfixed inode.c) | **P1, P2 FAIL** | **P2 fails** (needs 1/2) |
| B lookup inductive, any size | **P1 FAILS** | **all pass** (284 checks) |
| C `squashfs_read_inode`, all types | **P3 FAILS** (returns 40) | **all pass** (788 checks) |
| D `squashfs_readdir` | pass | pass |
| P5 (both versions) | fails | fails → fixed by one-word change, see §4 |

Counterexamples found on the unfixed code:

1. `i_size = 9`, no index: the first `squashfs_dir_space()` check fails before any read, so `err` is
   **uninitialized** and lookup returns `ERR_PTR(stack garbage)`. Neither Chen's commit message nor
   the syzbot reports mention this trigger; Chen's patch covers it anyway.
2. `i_size >= 15`: header read succeeds (`err = 12`), next check fails → `ERR_PTR(12)`.
3. entry read succeeds (`err = 8`) → `ERR_PTR(8)` (the case Chen's message describes).
4. `squashfs_read_inode()` for a long directory with `i_size = 1` returns 40 (32 for a basic
   directory) → `squashfs_iget()` returns `ERR_PTR(40)` → crash in `d_obtain_alias()` (export) **or in
   `d_splice_alias()` (ordinary lookup)**.

Conclusions:

* **Both of Chen's patches are needed, and together they are sufficient** for P1–P4 at every
  directory size. PATCH 2/2 alone leaves the lookup path crashable through `squashfs_iget()`
  (row "iget mode 1").
* `dir.c` has the same three jumps, but its error label returns 0, so no gap there (D).
* `squashfs_dir_space()` does its job: R1–R3 hold in every run.

## 4. Dead `u64 block < 0` check (already fixed upstream)

`squashfs_lookup()` (patch 11/22) declares

```c
u64 block = squashfs_dir_start(squashfs_i(dir)->start, msblk);
...
if (block < 0)            /* gcc -Wtype-limits: always false */
        goto data_error;
```

`squashfs_dir_start()` returns `-EINVAL` as an `s64`; stored in a `u64`, the check can never fire, so
the directory-location sanity check from patch 11 is dead in lookup (P5 fails). `readdir` declares
`s64 block` and is correct. Consequence: no crash (the bogus location fails later in
`squashfs_read_data()` via patch 22's `S64_MAX` check), but the patch 11 check is ineffective on
this path. Changing the declaration to `s64 block` makes P5 pass with all other properties intact
(run `A_lookup_chen_plus_s64_block`).

Andrew Morton queued `squashfs-sanity-check-directory-metadata-block-location-fix.patch`
(mm-nonmm-unstable, 2026-11-16), which casts `(s64)block`. It is included in the final tree (§7)
and P5 passes there, so no separate patch is needed.

## 6. Andrew Morton's queued fix leaves one path open

`squashfs-fix-variables-in-directory-code-and-add-more-sanity-checks-fix.patch` (mm-nonmm-unstable,
2026-11-16, after a smatch report by Dan Carpenter) sets `err = -ENOSPC` on the **entry** and **name**
`squashfs_dir_space()` checks. The **directory header** check before the header read still jumps to
`read_failure` without setting `err`. Model-checking `squashfs_lookup()` exactly as queued:

| run | result | counterexample |
|---|---|---|
| F bounded | **P1 fails** | 15-byte directory → `ERR_PTR(12)`; 9-byte directory → `ERR_PTR(uninitialised)` |
| F inductive | **P1 fails** | same path, any size |

`inode.c` is not fixed anywhere in the queue, so Chen's PATCH 1/2 is still required. Chen's PATCH 2/2
overlaps Andrew's fix and will not apply on top of it. Its first hunk is the missing header fix.

``0001-squashfs-set-an-error-code-on-the-directory-header-b.patch`` is that one hunk,
rebased onto Andrew's queue (`goto data_error`, i.e. `-EIO`). The commit message credits Chen's earlier
posting of the same hunk.

## 7. Final tree: Andrew's two fixes + the patch + Chen PATCH 1/2

Every property, on the code as it would merge (`run_final.sh`):

| run | checks | result |
|---|---|---|
| G lookup, bounded (all loops unwound) | 279 | pass (P1, P2, P5, R1–R3, memory safety) |
| G lookup, inductive (any `i_size` < 2^32, any index count) | 284 | pass |
| G `squashfs_read_inode`, all inode types | 788 | pass (P3) |
| G `readdir`, bounded | 278 | pass |
| G `readdir`, inductive (any size); end reachable | 295 | pass |
| G callee contracts, from real source | 103 | pass |

Callee contracts, proven on the real functions (previously assumed):

* **C1** `squashfs_read_metadata()` (cache.c) returns exactly `length` or `-errno`, for every read size
  up to 260 bytes (all reads lookup and readdir make).
* **C2** on success `*offset` stays inside a metadata block.
* **C3** `squashfs_iget()` returns a real inode or `ERR_PTR(-errno)`, given P3.

C3 combined with P3 discharges the `squashfs_iget()` assumption used by the lookup proofs, and C1 discharges the
`squashfs_read_metadata()` assumption.

## 8. Sweep of the whole 22-patch series

All 69 error exits the series adds (block.c, cache.c, export.c, file.c, fragment.c, id.c, xattr.c,
xattr_id.c, super.c, dir.c, namei.c, inode.c) were inspected. Apart from Chen's five sites (the
three `namei.c` space checks and the two `inode.c` size checks), every one returns an explicit `-E…` or a value
just tested negative. Patch 15 makes `squashfs_get_fragment()` able to return `ERR_PTR`; both callers
(`squashfs_readpage_fragment`, `squashfs_readahead_fragment`) handle it.

The `count + 1` / `size + 1` uint32 wrap (raised separately) is reachable from a crafted image but breaks
no property above, and the expressions predate the series (they are in mainline).

## 9. Limits of this verification

* Source-level proof of the four functions against callee contracts; not a run of the syzbot C
  reproducers on a built kernel. A boot test with the three reproducers is still the confirmation
  syzbot needs before the reports close.
* Callees below `squashfs_read_metadata()` (cache, decompressor, block I/O) are modelled by contract:
  a cache entry has 1–8192 bytes and `error` is 0 or `-errno`.
* VFS functions (`d_splice_alias`, `iget_locked`) are modelled from their documented behaviour.
* Andrew's queued patches were transcribed from the mm-commits notices, since kernel.org is not reachable here.

## Reproduce

`./run_all.sh` then `./run_final.sh` (needs git, python3, gcc, cbmc ≥ 5.95). Rebuilds the tree, checks
the hashes, and regenerates every row above into `results/`. About 13 minutes on 2 cores.
