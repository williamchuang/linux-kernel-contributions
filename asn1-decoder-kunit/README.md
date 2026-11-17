# ASN.1 BER/DER decoder — KUnit suite and verification kit

A KUnit test suite for the kernel's ASN.1 BER/DER decoder (`lib/asn1_decoder.c`,
`asn1_ber_decoder`), together with an independent verification kit that checks
the decoder's memory safety with **eight methods** across three families, and a
reproducible **regression-detection demonstration** of the suite itself.

**Status:** submitted as an RFC on 2026-11-17; builds and passes under
`kunit.py` (13/13); under maintainer review.
Thread: https://lore.kernel.org/all/20261117001711.47722-1-founder@logarcheon.com/

No over-read was found by any verification method, so the deliverable is the
test suite, not a bug fix.

**Licensing:** this folder mixes original work and verbatim GPL-2.0 kernel
sources — see `NOTICE`.

## The two safety invariants

- **I1 — no over-read:** the read cursor never passes the input buffer end.
- **I2 — slice validity:** every `(value, vlen)` slice handed to an action
  callback lies inside the buffer.

## The eight verification methods

*Family A — proofs on an abstract model (unbounded):*

1. **Coq proof** (`coq/Asn1Safety.v`) — machine-checked by `coqc`; proves I1/I2
   and invariant preservation for all inputs on the model.
2. **Lean proof** (`lean/Asn1Safety.lean`) — the same proof for a Lean 4
   toolchain.
3. **Executable model-check** (`lean/model_check.py`) — runs the identical
   abstract machine over an exhaustive small-state sweep plus 200k deep random
   runs; it caught a real gap in an earlier draft of the invariant (missing
   stack monotonicity).

*Family B — proof on the real C (bounded):*

4. **CBMC** (`cbmc/harness_fixed.c`) — bounded model checking of the actual
   `asn1_ber_decoder`; a completeness-checked proof of I1/I2 for all inputs of
   length up to 2 bytes, including the 64-bit `size_t` arithmetic.

*Family C — exhaustive and stress execution on the real C:*

5. **Exhaustive enumeration** (`src/enum_verify.c`) — every byte string of
   length 0..3 (16,843,009 inputs) under a `PROT_NONE` guard page.
6. **Randomized + grammar-biased + crafted stress** (`src/stress_verify.c`) —
   120,000,000 inputs plus a crafted adversarial corpus, guard-page checked.
7. **libFuzzer coverage-guided fuzzing** (`fuzz/fuzz_asn1.c`) — 46,106,271
   executions under AddressSanitizer + UBSan, edge coverage saturated.
8. **AddressSanitizer + UBSan run** (`src/stress_verify.c` built with
   `-fsanitize=address,undefined`) — 16,000,000 inputs, clean.

Plus **mutation testing** (`src/mut_M1.c`, `mut_M2.c`, `mut_M3.c`) to confirm
the harness has detection power: three injected over-reads, all caught, while
the unmodified decoder stays clean across 120M+ guard-page inputs and 46M
fuzzer executions.

Complete results and exact commands are in `VERIFICATION.md`.

## Regression demonstration (the suite catches a reintroduced over-read)

The verification above hardens the *decoder*. This demonstrates that the
submitted *test suite* is not vacuous: it detects a reintroduced over-read in a
path it exercises. Details and logs are in `REGRESSION-DEMO.md`.

Method: build the suite in-tree under `kunit.py` with `CONFIG_KASAN=y`
(confirmed enabled in the generated `.config`), then weaken one bounds check in
`asn1_decoder.c` (`regression.diff`) so a short over-read is allowed.

| State | Result |
|---|---|
| unmodified decoder | **PASS 13/13** (`clean-run.log`) |
| one bounds check weakened | **FAIL 12/13** — `asn1_test_truncated_content` fails its return-value check: the decoder accepts a truncated SEQUENCE (returns 0 instead of `-EBADMSG`) (`regressed-run.log`) |
| change reverted | **PASS 13/13** |

What this establishes: the suite detects at least one real class of regression
(a relaxed bounds check that turns a rejection into an over-read) in an
exercised path. The failure surfaced as a KUnit assertion, not a KASAN report —
the crafted case hits the path deterministically. It does **not** establish
complete memory-safety coverage.

## Layout

| Path | What it is |
|---|---|
| `VERIFICATION.md` | the complete report, methods and results |
| `REGRESSION-DEMO.md` | the regression-detection demonstration and logs |
| `regression.diff` | the exact one-line change used for the demonstration |
| `clean-run.log`, `regressed-run.log` | the PASS 13/13 and FAIL 12/13 runs |
| `upstream/asn1_decoder_kunit.c` | the KUnit suite as submitted |
| `upstream/asn1_test.asn1` | the test grammar |
| `upstream/PLACEMENT_AND_RUN.md` | where the suite goes in-tree and how to run it |
| `src/` | userspace port, harnesses, mutants, kernel stubs |
| `coq/Asn1Safety.v` | Coq proof |
| `lean/Asn1Safety.lean`, `lean/model_check.py` | Lean proof and executable model-check |
| `cbmc/` | CBMC harnesses and prelude |
| `fuzz/` | libFuzzer target and seed corpus |

## Reproduce

In-kernel suite and the regression demonstration, from a kernel tree with the
RFC patch applied:

```sh
printf 'CONFIG_KUNIT=y\nCONFIG_ASN1_DECODER_KUNIT_TEST=y\nCONFIG_KASAN=y\nCONFIG_KASAN_GENERIC=y\n' > /tmp/asn1.kunitconfig
./tools/testing/kunit/kunit.py run --kunitconfig=/tmp/asn1.kunitconfig asn1_decoder   # PASS 13/13
git apply regression.diff
./tools/testing/kunit/kunit.py run --kunitconfig=/tmp/asn1.kunitconfig asn1_decoder   # FAIL 12/13
git checkout lib/asn1_decoder.c                                                        # revert
```

The eight verification methods have their own commands in `VERIFICATION.md`; in
brief:

```sh
cd coq  && coqc Asn1Safety.v            # Family A: machine-checked proof
cd ../lean && python3 -I model_check.py # Family A: executable model-check
cd ../cbmc && cbmc harness_fixed.c ../src/asn1_decoder.c ../src/asn1_test.asn1.c \
                -I ../src -I ../src/inc -DLEN=2 --unwind 5 --unwinding-assertions --bounds-check
cd ../src  && gcc -O2 -Iinc -include stddef.h enum_verify.c asn1_decoder.c asn1_test.asn1.c -o enum_verify && ./enum_verify 3
```

## Provenance

The decoder sources under `src/` (`asn1_decoder.c`, `asn1_compiler.c`,
`asn1.h`, `asn1_ber_bytecode.h`, `asn1_decoder.h`) are copied from the mainline
Linux tree (`torvalds/linux`); the RFC patch and the regression demonstration
sit on Linux 7.4-rc3 (`aebb727447640`). The `mut_M*.c` files, and
`regression.diff`, are intentionally-broken changes used only to confirm
detection power.
