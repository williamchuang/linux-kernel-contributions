# gfs2: NULL pointer dereference in gfs2_seek_data/gfs2_seek_hole

**Status:** applied by the gfs2 maintainer (Andreas Gruenbacher) on 2026-11-17;
heading to mainline via the gfs2 tree, with a stable backport (`Cc: stable`).
**Reported by:** syzbot. **Root-cause + v1:** William Chuang (Logarchéon Inc.).
**Final form of the fix:** gfs2 maintainer Andreas Gruenbacher (`Suggested-by:`).
**Applied:** https://lore.kernel.org/all/20261116232409.46988-1-founder@logarcheon.com/

- syzbot bug: https://syzkaller.appspot.com/bug?extid=5a5f492ccae698fd7434
- mailing-list thread (lore): https://lore.kernel.org/all/20261116180002.44336-1-founder@logarcheon.com/T/

## The bug

A general protection fault in `gfs2_glock_dq`, reachable from userspace by an
`lseek(SEEK_DATA/SEEK_HOLE)` on a gfs2 file whose filesystem has been withdrawn.
KASAN reports a null-ptr-deref in range `[0x20-0x27]` — the `gl_lockref.lock`
offset inside `struct gfs2_glock`.

## Root cause

When the filesystem has been withdrawn, `gfs2_glock_nq()` returns an error
without queuing the holder, and `gfs2_glock_nq_init()` then calls
`gfs2_holder_uninit()`, which sets `gh.gh_gl` to NULL. `gfs2_seek_data()` and
`gfs2_seek_hole()` called `gfs2_glock_dq_uninit()` regardless of the outcome, so
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

2. **Machine-checked Coq proof** (`Gfs2Holder.v`). A model of the control-flow
   property the fix establishes, checked by `coqc` (Coq 8.18):
   - `fixed_is_safe` — the `goto out` flow dequeues only an *acquired* holder,
     so `gh_gl` is never NULL at `gfs2_glock_dq_uninit`;
   - `buggy_is_unsafe` — the old flow dequeues an uninitialised holder on the
     failure path, i.e. the reported NULL dereference.

   ```sh
   coqc Gfs2Holder.v        # exit 0 = both theorems accepted
   ```

3. **syzbot C reproducer** (`REPRODUCE.md`). Runtime confirmation: the crash
   appears on an unpatched KASAN kernel and is gone with the patch applied. The
   reproducer and kernel config are linked from the syzbot bug page; steps to
   build and run them in a VM are in `REPRODUCE.md`. (Running this is optional —
   the maintainer has already reviewed and accepted the fix.)

## Files

| File | What it is |
|---|---|
| `v2-0001-gfs2-seek-fix.patch` | the submitted patch (maintainer's final form, carried forward under the author's Signed-off-by) |
| `Gfs2Holder.v` | the machine-checked Coq proof of the holder-lifecycle property |
| `REPRODUCE.md` | download links and VM steps for the syzbot reproducer |
| `README.md` | this file |

## Companion contribution

A separate, heavier piece of work in this repository — the ASN.1 BER/DER
decoder KUnit suite (`../asn1-decoder-kunit/`) — is verified with **eight
methods** across three families:

- *Proofs on an abstract model:* (1) Coq proof, (2) Lean proof, (3) an
  executable model-check.
- *Proof on the real C:* (4) CBMC bounded model checking of the actual decoder.
- *Exhaustive and stress execution:* (5) exhaustive enumeration under a guard
  page, (6) randomized + grammar-biased + crafted stress under a guard page,
  (7) libFuzzer coverage-guided fuzzing under ASan/UBSan, (8) an AddressSanitizer
  + UBSan run — with mutation testing to confirm the harness has detection power.

Those eight methods describe the ASN.1 decoder work, not this gfs2 fix.
