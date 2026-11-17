# Linux kernel contributions

Verified contributions to the mainline Linux kernel from Logarchéon Inc.
Each directory documents one submission: the patches as sent to LKML, the
root-cause analysis, and the verification artifacts.

| Contribution | Subsystem | Status | Archive |
|---|---|---|---|
| [squashfs lookup: error code on directory header check](squashfs-lookup-errcode/) | `fs/squashfs/namei.c` | Submitted — under review (2026-11-16) | [lore](https://lore.kernel.org/all/20261116233530.47177-1-founder@logarcheon.com/) |
| [gfs2 SEEK_DATA/SEEK_HOLE NULL-deref fix](gfs2-seek-nullderef/) | `fs/gfs2/inode.c` | v2 submitted — under review (2026-11-16) | [lore](https://lore.kernel.org/all/20261116180002.44336-1-founder@logarcheon.com/T/) |
| [ASN.1 decoder KUnit suite + verification kit](asn1-decoder-kunit/) | `lib/asn1_decoder.c` | RFC submitted — under review (2026-11-16) | [lore](https://lore.kernel.org/all/20261117001711.47722-1-founder@logarcheon.com/) |
| [assoc_array root-shortcut split fix](assoc_array-root-shortcut-fix/) | `lib/assoc_array.c` | Withdrawn (2026-11-16) — subsystem being replaced upstream | [lore](https://lore.kernel.org/all/20261116054112.39078-1-founder@logarcheon.com/) |

## Verification methods

Each folder's `VERIFICATION.md` or `README.md` gives the methods and exact
commands. In brief:

- **squashfs:** CBMC model checking of the real lookup, `readdir` and inode
  code, bounded and inductive (no size bound), on a hash-checked
  reconstruction of the tree under review.
- **gfs2:** root-cause analysis from the KASAN report and a machine-checked Coq
  proof of the holder-lifecycle property.
- **ASN.1 decoder:** eight methods in three families: Coq and Lean proofs plus
  an executable model-check on an abstract model; CBMC on the real C;
  exhaustive enumeration, randomized stress, libFuzzer and ASan/UBSan runs,
  with mutation testing to confirm the harness catches injected over-reads.
- **assoc_array:** exhaustive enumeration and randomized stress on a userspace
  port of the real C, a KUnit regression test, KASAN, and a Lean model.

## Author

William Chuang · Logarchéon Inc. · founder@logarcheon.com
