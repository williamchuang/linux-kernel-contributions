# ASN.1 KUnit suite — regression-detection demonstration

This shows the suite catches a reintroduced over-read in an exercised path.
It is a regression demonstration, not a proof of complete memory safety.

## Setup

- Base: Linux 7.4-rc3 (`aebb727447640`) + the RFC patch
  `[RFC PATCH] lib/asn1_decoder: add KUnit test suite`.
- Test tool: in-tree KUnit via `tools/testing/kunit/kunit.py`, built for UML
  on a 64-bit host (`arch/um` selects `HAVE_ARCH_KASAN if X86_64 && MMU`, so
  KASAN is valid here).
- `.kunitconfig`:

      CONFIG_KUNIT=y
      CONFIG_ASN1_DECODER_KUNIT_TEST=y
      CONFIG_KASAN=y
      CONFIG_KASAN_GENERIC=y

  `CONFIG_KASAN=y` and `CONFIG_KASAN_GENERIC=y` were confirmed present in the
  generated `.config` after `olddefconfig` (not silently dropped).

- Command:

      ./tools/testing/kunit/kunit.py run \
        --kunitconfig=.kunitconfig --build_dir=<dir> asn1_decoder

## Result 1 — unmodified decoder: PASS (13/13)

    ================ asn1_decoder (13 subtests) ================
    [PASSED] asn1_test_decode_valid
    ... (all 13) ...
    [PASSED] asn1_test_random_stress
    ================== [PASSED] asn1_decoder ===================
    Testing complete. Ran 13 tests: passed: 13

## Result 2 — one bounds check weakened: FAIL (12/13)

Regression applied (`regression.diff`): the primitive-element content-length
check is relaxed to allow up to 16 bytes past the buffer.

    --- a/lib/asn1_decoder.c
    +++ b/lib/asn1_decoder.c
    @@ -281,7 +281,7 @@
     		} else {
    -			if (unlikely(len > datalen - dp))
    +			if (unlikely(len > datalen - dp + 16))  /* INJECTED REGRESSION */
     				goto data_overrun_error;
     		}

Run:

    # asn1_test_truncated_content: EXPECTATION FAILED at lib/tests/asn1_decoder_kunit.c:112
    Expected asn1_ber_decoder(...) == -74, but
             asn1_ber_decoder(...) == 0 (0x0)
    [FAILED] asn1_test_truncated_content
    ... (other 12 PASSED) ...
    # Totals: pass:12 fail:1 skip:0 total:13
    Testing complete. Ran 13 tests: passed: 12, failed: 1

The weakened decoder accepts a SEQUENCE whose contents are truncated (returns
0) instead of rejecting it with `-EBADMSG` (`-74`), and `asn1_test_truncated_content`
catches that.

## Result 3 — change reverted: PASS (13/13)

`git checkout lib/asn1_decoder.c`, rebuild, run — back to 13/13.

## What this does and does not establish

- Establishes: the suite detects at least one real class of regression (a
  relaxed bounds check that turns a rejection into an over-read) in a path it
  exercises.
- The failure surfaced as a KUnit **assertion** failure (expected `-EBADMSG`,
  decoder returned 0), not a KASAN report — the crafted case hits the path
  deterministically. KASAN was enabled but this particular over-read landed in
  adjacent read-only data without a redzone, so it did not trip a sanitizer
  report; the assertion caught it instead.
- Does not establish: comprehensive memory-safety coverage, or that the suite
  would catch every possible over-read.

## Files in this folder

- `regression.diff` — the exact one-line change used for Result 2.
- `kunitconfig` — the config used (with KASAN).
- `clean-run.log` — Result 1.
- `regressed-run.log` — Result 2.
