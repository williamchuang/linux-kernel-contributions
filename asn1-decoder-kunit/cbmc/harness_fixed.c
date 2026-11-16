/* CBMC harness, fixed-length variant.
 *
 * For a compile-time LEN, `data` is an array of exactly LEN nondeterministic
 * bytes and the decoder is told datalen == LEN.  CBMC's --bounds-check then
 * flags any read past data[LEN-1] (an over-read, I1) for every byte value;
 * the callbacks assert slice validity (I2).  Sweeping LEN = 1..N proves the
 * property for all inputs of length up to N, far more cheaply than a
 * nondeterministic buffer size.
 */
#include <stddef.h>
#include <stdint.h>
#include "asn1_test.asn1.h"

struct cbmc_ctx { const unsigned char *base; size_t len; };

static void slice_ok(struct cbmc_ctx *c, const void *v, size_t vl)
{
	if (vl) {
		__CPROVER_assert((const unsigned char *)v >= c->base,
				 "I2: slice starts inside buffer");
		__CPROVER_assert((const unsigned char *)v + vl <= c->base + c->len,
				 "I2: slice ends inside buffer");
	}
}
int asn1_test_note_int(void *ctx, size_t h, unsigned char t, const void *v, size_t vl)
{ (void)h; (void)t; slice_ok(ctx, v, vl); return 0; }
int asn1_test_note_str(void *ctx, size_t h, unsigned char t, const void *v, size_t vl)
{ (void)h; (void)t; slice_ok(ctx, v, vl); return 0; }

#ifndef LEN
#define LEN 6
#endif

int main(void)
{
	unsigned char data[LEN];	/* exactly LEN nondeterministic bytes */
	struct cbmc_ctx c = { data, LEN };
	asn1_ber_decoder(&asn1_test_decoder, &c, data, LEN);
	return 0;
}
