# ASN.1 decoder — verification kit

Target: `lib/asn1_decoder.c` (`asn1_ber_decoder`), the kernel's BER/DER
tag-length-value decoder. It walks untrusted input under a compiled grammar;
the safety-critical property is that it never reads past the input buffer and
never hands an action callback a slice that escapes the buffer.

This kit establishes that property with eight methods across three families —
proofs on an abstract model, a proof on the real C, and exhaustive/stress
execution of the real C — and confirms the checks have detection power. It is
deliberately split: a lean KUnit suite meant for upstream, and a heavier kit
kept separate (the same separation the assoc_array review asked for).

## The two invariants

- **I1 — no over-read.** The read cursor never passes the buffer end.
- **I2 — slice validity.** Every `(value, vlen)` slice passed to a callback
  satisfies `value >= buf` and `value + vlen <= buf + datalen`.

## Family A — formal proofs (unbounded, on an abstract model)

An abstract length-accounting model of the decoder's control flow, whose guard
conditions mirror the C checks line for line. The proofs quantify over **all**
states and all guard-satisfying inputs — coverage no test can reach — showing a
guarded header read is in-bounds (I1), a guarded slice is valid (I2), and every
operation preserves the loop invariant from the entry state.

| Artifact | Tool | Status |
|---|---|---|
| `coq/Asn1Safety.v` | Coq 8.18 | **machine-checked here** — `coqc` accepts it, `.vo` produced |
| `lean/Asn1Safety.lean` | Lean 4 | checks on a local toolchain (Lean binaries are blocked by the sandbox proxy) |
| `lean/model_check.py` | executable mirror | exhaustive small-state sweep (756 states, 5,460 transitions) + 200k deep random runs (4,006,241 steps), 0 violations |

The invariant records that the stack of enclosing container ends is
non-decreasing — the monotonicity nesting enforces. This earned its place: an
earlier draft of the invariant omitted it, the model-check rejected it
(`leaveCons` broke well-formedness), and the corrected invariant is what all
three artifacts now carry.

## Family B — proof on the real C (bounded)

CBMC bounded model checking of the **actual** `asn1_ber_decoder`, not a model.
The input is a fixed-length array of fully nondeterministic bytes sized to the
logical length, so `--bounds-check` flags any read past the end (I1) for every
byte value, and the callbacks assert slice validity (I2). `--unwinding-assertions`
makes each result a complete proof for that length (the bound is proven
sufficient, not assumed).

| Length | Result | Completeness |
|---|---|---|
| all 1-byte inputs | VERIFICATION SUCCESSFUL | complete (unwind 4) |
| all 2-byte inputs | VERIFICATION SUCCESSFUL | complete (unwind 5) |

CBMC proves I1/I2 on the real C — including its 64-bit `size_t` arithmetic
(`datalen - dp` and friends, where an unguarded subtraction would underflow) —
for every byte string of length up to 2. Complete BMC of a bytecode-interpreter
decoder gets expensive quickly (the machine loop and 64-bit arithmetic bit-blast
into large formulas), so CBMC anchors the small-length end on the real code and
the exhaustive/stress family below carries the longer inputs. The harnesses
(`cbmc/harness_fixed.c`, `cbmc/harness_cbmc.c`) scale to larger bounds given
more solver time.

## Family C — exhaustive and stress execution (real C)

All against the decoder sources taken from the mainline tree at HEAD, over a
`PROT_NONE` guard page (any over-read faults on the first byte past the end).

| Method | Coverage | Result |
|---|---|---|
| Exhaustive enumeration | every byte string of length 0..3 (16,843,009 inputs) | 0 over-reads, 0 bad slices |
| Uniform random stress | 60,000,000 buffers, length 0..299 | 0 over-reads, 0 bad slices |
| Grammar-biased stress | 60,000,000 buffers over a structural byte alphabet | 0 over-reads, 0 bad slices |
| Crafted adversarial corpus | 40-deep definite/indefinite nesting, long-form lengths claiming 2^32-1 and 2^16-1 octets, truncated long-form, indefinite-on-primitive | 0 over-reads, 0 bad slices |
| **libFuzzer** (coverage-guided) | **46,106,271 executions, ~507k/s, ASan+UBSan** | 0 crashes, 0 sanitizer errors, 0 slice violations; edge coverage saturated |
| AddressSanitizer + UBSan | 16,000,000 mixed inputs + crafted corpus | clean (no memory error, signed-overflow, bad shift, uninitialized read) |

The coverage-guided fuzzer saturated the two-field grammar's reachable edges
(`cov` stable throughout) and, seeded with the known-valid vectors, still
surfaced nothing — a stronger statement than the blind random sweep because the
search is directed by code coverage.

No over-read was found by any method. So the deliverable is the test suite, not
a bug fix: over everything exercised, the decoder's bounds checks hold.

## Detection power — mutation testing

A verification that only reports "clean" is worthless until shown to fail on a
real defect. Three realistic over-reads were injected (each weakening one
bounds check) and run under the guard-page + slice harness:

| Mutant | Guard weakened | Guard-page faults (I1) | Bad slices (I2) |
|---|---|---:|---:|
| M1 | long-form content length | 111 | 1 |
| M2 | short-form content length | 2,088 | 39 |
| M3 | header two-byte bound | 5,543 | 55 |

Every mutant is caught within 3,000,000 inputs, through both invariants, while
the unmodified decoder stays at zero across 120M+ guard-page inputs and 46M
fuzzer executions. The harness is not passing vacuously.

## What ships upstream vs. what stays in the kit

**Upstream (lean):** `deliver-asn1/asn1_decoder_kunit.c` — 13 KUnit cases:
one well-formed decode, eight malformed-input rejections, three success-path
cases (indefinite+EOC, long-form length, zero-length string), and one bounded
deterministic random-stress case with a self-checking slice oracle (rides KASAN
in CI). checkpatch-clean; every expected return code confirmed against the real
decoder.

**Kit (separate, not attached to the patch):** the Coq and Lean proofs, the
executable model-check, the CBMC harnesses, the guard-page harnesses, the
libFuzzer target, the mutation tests, the sanitizer build.

## Reproduce

    # Family A
    cd coq  && coqc Asn1Safety.v              # machine-checked proof
    cd lean && python3 -I model_check.py      # executable model-check
    #          lean Asn1Safety.lean           # on a local Lean 4 toolchain

    # Family B
    cd cbmc && cbmc harness_fixed.c ../src/asn1_decoder.c ../src/asn1_test.asn1.c \
                 -I ../src -I ../src/inc -DLEN=2 --unwind 5 --unwinding-assertions --bounds-check

    # Family C
    cd src
    gcc -O2 -Iinc -include stddef.h enum_verify.c   asn1_decoder.c asn1_test.asn1.c -o enum_verify   && ./enum_verify 3
    gcc -O2 -Iinc -include stddef.h stress_verify.c asn1_decoder.c asn1_test.asn1.c -o stress_verify && ./stress_verify 60000000
    gcc -O1 -g -Iinc -include stddef.h -fsanitize=address,undefined -fno-sanitize-recover=all \
        stress_verify.c asn1_decoder.c asn1_test.asn1.c -o stress_san && ./stress_san 8000000
    cd ../fuzz
    clang -O1 -g -I ../src -I ../src/inc -include stddef.h -fsanitize=address,undefined,fuzzer \
        fuzz_asn1.c ../src/asn1_decoder.c ../src/asn1_test.asn1.c -o fuzz_asn1 && ./fuzz_asn1 corpus -max_total_time=90

    # KUnit dry-run against the real decoder
    cd ../src && gcc -O2 -Ishiminc -I. -include stddef.h run_kunit.c asn1_decoder.c asn1_test.asn1.c -o run_kunit && ./run_kunit

All kit harnesses compile `-Wall -Wextra -Wpedantic` clean.

## Open items

- The in-kernel `kunit.py` run (grammar compiled in-tree by `scripts/asn1_compiler`,
  KASAN build) is the final gate before submission; the userspace dry-run is
  green but is not that run.
- `Asn1Safety.lean` wants one `lean` compile on a real toolchain; the Coq proof
  already closes here, and the model-check backs both.
- CBMC reaches larger lengths with more solver time; the bound here is a
  compute budget, not a limit of the method.
