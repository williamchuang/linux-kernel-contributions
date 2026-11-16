/* CBMC harness: prove asn1_ber_decoder() never reads outside its input.
 *
 * The input is a malloc'd object sized EXACTLY to the logical length `len`,
 * with fully nondeterministic bytes.  CBMC's bounds/pointer checks then flag
 * any read past data[len-1] -- i.e. any over-read -- for every byte value and
 * every len up to N.  The action callbacks additionally assert (I2) that each
 * (value, vlen) slice they are handed lies inside [data, data+len).
 */
#include <stddef.h>
#include <stdint.h>
#include "asn1_test.asn1.h"

struct cbmc_ctx { const unsigned char *base; size_t len; };

int asn1_test_note_int(void *context, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct cbmc_ctx *c = context;
	(void)hdrlen; (void)tag;
	if (vlen) {
		__CPROVER_assert((const unsigned char *)value >= c->base,
				 "I2: int slice starts inside buffer");
		__CPROVER_assert((const unsigned char *)value + vlen
				 <= c->base + c->len,
				 "I2: int slice ends inside buffer");
	}
	return 0;
}

int asn1_test_note_str(void *context, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct cbmc_ctx *c = context;
	(void)hdrlen; (void)tag;
	if (vlen) {
		__CPROVER_assert((const unsigned char *)value >= c->base,
				 "I2: str slice starts inside buffer");
		__CPROVER_assert((const unsigned char *)value + vlen
				 <= c->base + c->len,
				 "I2: str slice ends inside buffer");
	}
	return 0;
}

#ifndef N
#define N 6
#endif

size_t nondet_len(void);

int main(void)
{
	size_t len = nondet_len();
	__CPROVER_assume(len <= (size_t)N);

	unsigned char *data = malloc(len);	/* object size == len; bytes nondet */
	__CPROVER_assume(len == 0 || data != NULL);

	struct cbmc_ctx c = { data, len };
	asn1_ber_decoder(&asn1_test_decoder, &c, data, len);
	return 0;
}
