# lib/assoc_array: root-shortcut split lookup fix

A bug fix and KUnit test suite for the Linux kernel's generic associative
array (`lib/assoc_array.c`), submitted to LKML.

**Status:** Withdrawn (2026-11-16)
**Archive:** https://lore.kernel.org/all/20261116054112.39078-1-founder@logarcheon.com/
**Author:** William Chuang <founder@logarcheon.com> · Logarchéon Inc.

## The bug

When a root shortcut is split at the first level-segment boundary, the
replacement node is installed one segment too shallow. Lookups then walk
the tree at the wrong depth, so a fraction of previously inserted keys can
no longer be found, even though they remain in the array.

## The fix

A one-line correction to the split condition in `assoc_array_insert()`, so
the new node is placed at the correct depth. See `patches/0002-*.patch`.

## Layout

- `patches/` — the two-patch series as sent to LKML
- `analysis/` — root-cause writeup and a self-contained reproducer
- `verify/` — the verification report and the userspace enumeration kit
- `lean/` — a Lean model of the split invariant

## Verification

- **Exhaustive enumeration.** A faithful userspace port of the real C, run
  over every fan-out-4 insertion pattern: the unpatched code loses keys;
  the patched code recovers all of them.
- **Randomized stress.** 200k randomized insertion runs, clean under the fix.
- **KUnit regression test.** Fails on the unpatched tree, passes with the
  fix; ships inside the patch series so the fix stays locked in.
- **KASAN.** Clean, no memory errors.
- **Lean model.** A model of the split invariant, under `lean/`.

Complete method writeup: `verify/VERIFICATION.md`.

## Reproduce

Build the in-tree test against a kernel checkout:

    tools/testing/kunit/kunit.py run assoc_array

Without the fix the root-shortcut-split case reports lost keys; with it
applied, the suite passes.

## License

GPL-2.0 throughout, matching the kernel tree.

## Status and review history

**Status:** v1 submitted to LKML on 2026-11-16, then withdrawn. Maintainer
feedback was that `assoc_array` is being replaced upstream by rhashtable, so
new tests and fixes for it are not wanted. The analysis and verification are
kept here as a record.

**Archive:** https://lore.kernel.org/all/20261116054112.39078-1-founder@logarcheon.com/

**Reviews**
- Automated review (Sashiko AI, on the public keyrings list) flagged that
  building the test as a module (`ASSOC_ARRAY_KUNIT_TEST=m`) cannot resolve
  the non-exported `assoc_array_*()` symbols.
- A v2 (built-in-only test, `depends on KUNIT=y`) was planned to fix the
  modular build, and was not sent after the withdrawal.
