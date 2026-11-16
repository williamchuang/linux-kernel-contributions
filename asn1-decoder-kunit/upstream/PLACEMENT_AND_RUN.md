# ASN.1 decoder KUnit suite — RFC v1

## Files and where they go in the kernel tree

| This file | Copy to |
|---|---|
| `asn1_test.asn1` | `lib/tests/asn1_test.asn1` |
| `asn1_decoder_kunit.c` | `lib/tests/asn1_decoder_kunit.c` |

## 1. Add to `lib/tests/Makefile`

    obj-$(CONFIG_ASN1_DECODER_KUNIT_TEST) += asn1_decoder_kunit_test.o
    asn1_decoder_kunit_test-y := asn1_decoder_kunit.o asn1_test.asn1.o
    $(obj)/asn1_decoder_kunit.o: $(obj)/asn1_test.asn1.h
    $(obj)/asn1_test.asn1.o: $(obj)/asn1_test.asn1.c $(obj)/asn1_test.asn1.h
    clean-files += asn1_test.asn1.c asn1_test.asn1.h

## 2. Add to `lib/Kconfig.debug` (near the other lib KUnit tests)

    config ASN1_DECODER_KUNIT_TEST
    	tristate "KUnit tests for the ASN.1 decoder" if !KUNIT_ALL_TESTS
    	depends on KUNIT
    	select ASN1
    	default KUNIT_ALL_TESTS
    	help
    	  Enable this option to test the ASN.1 BER/DER decoder
    	  (lib/asn1_decoder.c) against well-formed and malformed input.

    	  For more information on KUnit and unit tests in general, please
    	  refer to the KUnit documentation in Documentation/dev-tools/kunit/.

    	  If unsure, say N.

## 3. Build and run under KUnit (no GPU, no special hardware)

From a kernel checkout:

    tools/testing/kunit/kunit.py run --kunitconfig=/dev/stdin asn1_decoder <<'EOF'
    CONFIG_KUNIT=y
    CONFIG_ASN1=y
    CONFIG_ASN1_DECODER_KUNIT_TEST=y
    EOF

Expected: the suite `asn1_decoder` runs 9 cases, all PASS.

## Cases (9)

1. decode_valid         — well-formed message parses; both actions fire with the right values
2. truncated_content    — SEQUENCE claims 7 octets, 6 present            -> -EBADMSG
3. length_overrun       — INTEGER length runs past the SEQUENCE          -> -EBADMSG
4. long_tag             — high-tag-number (long) form is unsupported     -> -EBADMSG
5. wrong_tag            — top-level tag is not the expected SEQUENCE      -> -EBADMSG
6. too_large            — datalen above the 65535 ceiling                -> -EMSGSIZE
7. short_header         — buffer too short for one tag+length header      -> -EBADMSG
8. missing_eoc          — indefinite-length constructed with no EOC      -> -EBADMSG
9. invalid_eoc          — EOC marker with a non-zero length octet        -> -EBADMSG
