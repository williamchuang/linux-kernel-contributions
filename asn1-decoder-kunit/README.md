# ASN.1 BER/DER decoder — KUnit suite and verification kit

A KUnit test suite for the kernel's ASN.1 BER/DER decoder (`lib/asn1_decoder.c`,
`asn1_ber_decoder`), together with an independent verification kit that checks
the decoder's memory safety with **eight methods** across three families.

**Status:** test suite prepared; pending an in-kernel `kunit.py` run (KASAN
build) before submission as an RFC. No over-read was found by any method, so the
deliverable is the test suite, not a bug fix.

**Licensing:** this folder mixes original work and verbatim GPL-2.0 kernel
sources — see `NOTICE`.

## The two safety invariants

- **I1 — no over-read:** the read cursor never passes the input buffer end.
- **I2 — slice validity:** every `(value, vlen)` slice handed to an action
  callback lies inside the buffer.

## The eight methods

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

## Layout

| Path | What it is |
|---|---|
| `VERIFICATION.md` | the complete report, methods and results |
| `upstream/asn1_decoder_kunit.c` | the KUnit suite prepared for submission |
| `upstream/asn1_test.asn1` | the test grammar |
| `upstream/PLACEMENT_AND_RUN.md` | where the suite goes in-tree and how to run it |
| `src/` | userspace port, harnesses, mutants, kernel stubs |
| `coq/Asn1Safety.v` | Coq proof |
| `lean/Asn1Safety.lean`, `lean/model_check.py` | Lean proof and executable model-check |
| `cbmc/` | CBMC harnesses and prelude |
| `fuzz/` | libFuzzer target and seed corpus |

## Reproduce

See `VERIFICATION.md` for the complete command list. In brief:

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
Linux tree (`torvalds/linux`); record the exact commit you built against. The
`mut_M*.c` files are intentionally-broken copies used only for mutation testing.
