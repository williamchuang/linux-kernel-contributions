# Linux kernel contributions

Verified contributions to the mainline Linux kernel from Logarchéon Inc.
Each directory documents one submission: the patches as sent to LKML, the
root-cause analysis, and the verification artifacts.

**Status (2026-11-17):** 6 submissions — **1 accepted** · **3 in review** · **2 withdrawn**.

| Contribution | Subsystem | Status | Archive |
|---|---|---|---|
| [ext4 swap-boot: reject a non-regular boot loader inode](ext4-swap-boot-inode-type/) | `fs/ext4/ioctl.c` | v2 submitted — under review (2026-11-17) | [lore](https://lore.kernel.org/all/20261117061822.3826-1-founder@logarcheon.com/) |
| [ext4 journal credit shortfalls (swap-boot, inline data)](ext4-journal-credits/) | `fs/ext4/ioctl.c`, `fs/ext4/inline.c` | Submitted — under review (2026-11-16) | [lore](https://lore.kernel.org/all/20261117052750.2955-1-founder@logarcheon.com/) |
| [squashfs lookup: error code on directory header check](squashfs-lookup-errcode/) | `fs/squashfs/namei.c` | Submitted — under review (2026-11-16) | [lore](https://lore.kernel.org/all/20261116233530.47177-1-founder@logarcheon.com/) |
| [GFS2 lseek NULL-pointer fix in `gfs2_glock_dq`](gfs2-glock-dq-nullderef/) | `fs/gfs2/inode.c` | Applied by the gfs2 maintainer (2026-11-17) | [lore](https://lore.kernel.org/all/20261116180002.44336-1-founder@logarcheon.com/T/) |
| [ASN.1 decoder KUnit suite + verification kit](asn1-decoder-kunit/) | `lib/asn1_decoder.c` | Withdrawn (2026-11-17) — maintainer judged an in-tree suite not worth its upkeep | [lore](https://lore.kernel.org/all/20261117001711.47722-1-founder@logarcheon.com/) |
| [assoc_array root-shortcut split fix](assoc_array-root-shortcut-fix/) | `lib/assoc_array.c` | Withdrawn (2026-11-16) — subsystem being replaced upstream | [lore](https://lore.kernel.org/all/20261116054112.39078-1-founder@logarcheon.com/) |

## Verification methods

Each folder's `VERIFICATION.md` or `README.md` gives the methods and exact
commands. In brief:

- **ext4 swap-boot inode type:** crafted images (inode 5 as a directory,
  with link count 1 and 0, or as a character device) run on User Mode
  Linux before and after the fix; the normal first use of the ioctl is
  checked to still work.
- **ext4:** standalone reproducers run on User Mode Linux before and after
  the fix (unpatched: journal abort, and for swap-boot an inconsistent
  filesystem; patched: no warning and a clean `e2fsck`).
- **squashfs:** CBMC model checking of the real lookup, `readdir` and inode
  code, bounded and inductive (no size bound), on a hash-checked
  reconstruction of the tree under review.
- **GFS2:** root-cause analysis from the KASAN report and a machine-checked Coq
  proof of the holder-lifecycle property.
- **ASN.1 decoder:** eight methods in three families: Coq and Lean proofs plus
  an executable model-check on an abstract model; CBMC on the real C;
  exhaustive enumeration, randomized stress, libFuzzer and ASan/UBSan runs,
  with mutation testing to confirm the harness catches injected over-reads.
- **assoc_array:** exhaustive enumeration and randomized stress on a userspace
  port of the real C, a KUnit regression test, KASAN, and a Lean model.

## Author

William Chuang · Logarchéon Inc. · founder@logarcheon.com
