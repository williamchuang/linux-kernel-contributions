/* libFuzzer entry for asn1_ber_decoder().
 *
 * Coverage-guided fuzzing of the real decoder under ASan + UBSan.  The input
 * bytes come straight from the fuzzer; the callbacks assert (I2) that every
 * (value, vlen) slice lies inside the buffer.  ASan catches any over-read
 * (I1) directly.  The decoder caps datalen at 65535, so inputs are truncated
 * to that.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "asn1_test.asn1.h"

struct fuzz_ctx { const unsigned char *base; size_t len; };

static void check_slice(struct fuzz_ctx *c, const void *v, size_t vl)
{
	const unsigned char *p = v;
	if (vl && (p < c->base || p + vl > c->base + c->len)) {
		fprintf(stderr, "I2 VIOLATION: slice escapes buffer\n");
		abort();
	}
}

int asn1_test_note_int(void *ctx, size_t h, unsigned char t,
		       const void *v, size_t vl)
{ (void)h; (void)t; check_slice(ctx, v, vl); return 0; }

int asn1_test_note_str(void *ctx, size_t h, unsigned char t,
		       const void *v, size_t vl)
{ (void)h; (void)t; check_slice(ctx, v, vl); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char *buf;
	struct fuzz_ctx c;

	if (size > 65535)
		size = 65535;

	/* Copy into an exact-size heap block so ASan red-zones bracket it:
	 * an over-read by even one byte lands in a red-zone and aborts. */
	buf = malloc(size ? size : 1);
	if (!buf)
		return 0;
	if (size)
		memcpy(buf, data, size);

	c.base = buf;
	c.len = size;
	asn1_ber_decoder(&asn1_test_decoder, &c, buf, size);

	free(buf);
	return 0;
}
