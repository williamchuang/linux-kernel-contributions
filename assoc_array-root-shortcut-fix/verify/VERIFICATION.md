# Verification — assoc_array root-shortcut-split bug and fix

Evidence behind two patches to `lib/assoc_array.c`:

1. `lib/assoc_array: add KUnit test suite`
2. `lib/assoc_array: fix lookups after splitting a root shortcut`

Base tree: Linux 7.4-rc3.

## 1. The defect

`assoc_array_find()` cannot reach most stored leaves after a shortcut at the
root of the tree is split at its first segment boundary. The leaves remain in
the tree — `assoc_array_iterate()` visits them all and `nr_leaves_on_tree` is
correct — so the data stays intact but becomes unfindable. Root cause and
mechanism: `assoc_array_find_bug.md`.

## 2. Methods

Three independent methods, each with a different reach and a different trust
basis.

| Method | Covers | Reach | Runs the real C |
| --- | --- | --- | --- |
| KUnit suite | insert/delete/find/gc/iterate vs a model | concrete + 200 random seeds | yes (in-kernel) |
| Exhaustive enumeration | find-after-split invariant | every input in a bounded family | yes (faithful userspace port) |
| Lean 4 proof | the split's level accounting | the abstract algorithm | no (model) |

Symbolic bounded model checking of the real C (CBMC) was attempted first. It did
not terminate even on a two-key insert: `assoc_array` encodes a pointer's type
in its low bits, and the constant casting between pointer and integer is
pathological for CBMC's pointer model. This is a limitation of symbolic model
checking on pointer-tagged code, not a tuning problem. Exhaustive enumeration is
the sound substitute: over a finite input family, running the real logic on
every member is as exhaustive as a symbolic check, and it completes in
milliseconds.

## 3. KUnit suite (patch 1)

`lib/tests/assoc_array_kunit.c`, 963 lines. Keeps a model of the expected
contents and, after every operation, checks that every present key is found,
every absent key is not, iteration visits each object once, and
`nr_leaves_on_tree` matches. Objects are reference counted like keyring keys, so
leaks and double frees surface.

Results on the clean tree:

- checkpatch `--strict`: 0 errors, 0 warnings, 0 checks.
- 24/24 pass; also under KASAN, UBSAN (shift/bounds), SLUB_DEBUG, DEBUG_OBJECTS
  (RCU-head), and sparse.
- Stress: 200 random-operation seeds + 300 garbage-collection rounds, all pass.
- Mutation testing: reverting each of four historical fixes (commits
  ea6789980fda, bb2ba2d75a2d, 8d4a2ec1e0b4, a82c8a05e86f) makes the suite fail or
  panic.

Reproduce: `./tools/testing/kunit/kunit.py run assoc_array`

## 4. Exhaustive enumeration on the real code

The real functions from `lib/assoc_array.c` compile into userspace behind a thin
shim that replaces only kernel macros (allocation, RCU, `BUG_ON` → `assert`,
pointer-tag helpers). The functions themselves are unmodified. The port is
faithful: it reproduces the 18-key failure identically to the in-kernel test
(`iterate=18, nr_leaves=18, findable=2`).

At fan-out 4 — the same control flow, one constant changed, so the bug triggers
with 6 keys instead of 18 — the enumeration covers a bounded family:

- Sweep 1: all 4^5 = 1024 assignments of the distinguishing segments, canonical
  insertion order.
- Sweep 2: all 6! = 720 insertion orders of the canonical assignment.

Running the genuine `insert` + `find` on every case:

| Code | Sweep 1 (1024) | Sweep 2 (720) |
| --- | --- | --- |
| mainline (buggy) | 1024 lose a key | 120 lose a key |
| with the fix | 0 | 0 |

No `assert`/`BUG_ON` fired in either sweep with the fix. A 200,000-run randomized
differential stress on the fixed port (fan-out 16, random clustered and random
keys, random insertion order) found no missing key and no assertion.

Reproduce: `verification_kit/run_verification.sh <path-to-linux-tree>`

## 5. Lean 4 proof (model level)

`AssocArrayRootShortcut.lean` models the tree as an inductive type with keys as
segment lists, defines `find` as the level-by-level walk, and models the split so
the buggy and fixed variants differ in exactly one thing: whether the
pre-shortcut covering the skipped leading segment is kept
(`buggy_is_fixed_without_preshortcut`). It proves, by kernel computation:

- `fixed_finds_all` — the fixed split keeps every key findable;
- `buggy_loses_keys` — the buggy split loses the diverging-prefix keys;
- `bug_and_fix` — the same stored key is unfindable after the buggy split and
  findable after the fix.

Scope: these are properties of the abstract model, not of `lib/assoc_array.c`.
The model drops pointer tagging, allocation and RCU. Its role is to pin the
meaning of the fix — the omitted pre-shortcut is the whole defect — as a
machine-checkable artifact. The real code is covered by §3–4. Check with
`lean AssocArrayRootShortcut.lean`.

## 6. The fix (patch 2)

One guarded condition in `assoc_array_insert_mid_shortcut()`: when the shortcut
being split is at the root (`back_pointer == NULL`), create the pre-shortcut that
covers the leading segment(s), so the new node is indexed at the correct level.
Non-root shortcuts are untouched — the added test short-circuits on
`back_pointer == NULL`. `Fixes: 3cb989501c26`, the 2013 commit that introduced
the structure.

Validation: the fan-out-4 enumeration (0 of 1744 cases fail, was 1024+120),
200,000 randomized port runs, the full KUnit suite plus the reproducer at fan-out
16, the 200-seed + 300-gc stress, and KASAN — all clean, no assertion tripped.

## 7. Limitations

- The KUnit suite and the enumeration are bounded. The enumeration is exhaustive
  only within its family (fan-out 4, 6 keys, the clustered-prefix shape); the
  randomized stress samples beyond it but is not exhaustive.
- The Lean proof concerns the model, not the C.
- The fix introduces a single-segment root pre-shortcut. Every test passes,
  including gc/delete/destroy and KASAN, but whether that shape is preferable to
  placing the node directly at the right level is a maintainer decision. The fix
  warrants review by whoever owns this code before it lands.
