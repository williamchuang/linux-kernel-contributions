// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit tests for the ASN.1 BER/DER decoder in lib/asn1_decoder.c.
 *
 * A tiny grammar (asn1_test.asn1: SEQUENCE { INTEGER, OCTET STRING }) drives
 * the decoder.  The cases feed well-formed and malformed DER byte strings to
 * asn1_ber_decoder() and check that valid input is parsed and the action
 * callbacks fire with the expected values, while each class of malformed input
 * is rejected (rather than over-read) with the documented error code.
 *
 * Copyright (C) 2026 Logarcheon Inc.
 */
#include <kunit/test.h>
#include <linux/asn1_decoder.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "asn1_test.asn1.h"

struct asn1_test_ctx {
	unsigned int	int_calls;
	unsigned int	str_calls;
	u8		int_first;
	size_t		str_len;
	u8		str_buf[8];
	/*
	 * Optional slice oracle.  When @buf is set, every callback checks that
	 * the (value, vlen) slice it is handed lies inside [buf, buf + buflen);
	 * the stress case uses this to catch a bad slice directly, without
	 * waiting for KASAN.  Tests that leave @buf NULL skip the check.
	 */
	const u8	*buf;
	size_t		buflen;
	bool		slice_oob;
};

static void asn1_test_check_slice(struct asn1_test_ctx *ctx,
				  const void *value, size_t vlen)
{
	const u8 *v = value;

	if (!ctx->buf || vlen == 0)
		return;
	if (v < ctx->buf || v + vlen > ctx->buf + ctx->buflen)
		ctx->slice_oob = true;
}

/*
 * Action callbacks referenced by asn1_test.asn1.  They must be non-static and
 * match the names used in the grammar; asn1_test.asn1.h declares them.
 */
int asn1_test_note_int(void *context, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct asn1_test_ctx *ctx = context;

	ctx->int_calls++;
	asn1_test_check_slice(ctx, value, vlen);
	if (vlen >= 1)
		ctx->int_first = ((const u8 *)value)[0];
	return 0;
}

int asn1_test_note_str(void *context, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct asn1_test_ctx *ctx = context;

	ctx->str_calls++;
	asn1_test_check_slice(ctx, value, vlen);
	ctx->str_len = vlen;
	if (vlen <= sizeof(ctx->str_buf))
		memcpy(ctx->str_buf, value, vlen);
	return 0;
}

/* SEQUENCE { INTEGER 0x2a, OCTET STRING "hi" } */
static const u8 asn1_test_der_valid[] = {
	0x30, 0x07,
		0x02, 0x01, 0x2a,
		0x04, 0x02, 0x68, 0x69,
};

/* A well-formed message is decoded and both actions fire with the right data. */
static void asn1_test_decode_valid(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	int ret;

	ret = asn1_ber_decoder(&asn1_test_decoder, &ctx,
			       asn1_test_der_valid,
			       sizeof(asn1_test_der_valid));
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx.int_calls, 1u);
	KUNIT_EXPECT_EQ(test, ctx.int_first, (u8)0x2a);
	KUNIT_EXPECT_EQ(test, ctx.str_calls, 1u);
	KUNIT_EXPECT_EQ(test, ctx.str_len, (size_t)2);
	KUNIT_EXPECT_MEMEQ(test, ctx.str_buf, "hi", 2);
}

/* The SEQUENCE header claims 7 content octets but only 6 are present. */
static void asn1_test_truncated_content(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x07,
			0x02, 0x01, 0x2a,
			0x04, 0x02, 0x68,	/* final octet missing */
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/* The INTEGER length (0x7f) runs past the end of the containing SEQUENCE. */
static void asn1_test_length_overrun(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x03,
			0x02, 0x7f, 0x00,
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/* A high-tag-number (long) tag form is not supported and must be rejected. */
static void asn1_test_long_tag(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x03,
			0x1f, 0x01, 0x00,	/* tag low 5 bits == 0x1f */
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/* A top-level tag that is not the expected SEQUENCE is rejected. */
static void asn1_test_wrong_tag(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x31, 0x07,			/* SET instead of SEQUENCE */
			0x02, 0x01, 0x2a,
			0x04, 0x02, 0x68, 0x69,
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/* datalen above the decoder's 65535-octet ceiling returns -EMSGSIZE. */
static void asn1_test_too_large(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	size_t len = 0x10000;		/* 65536 > 65535 */
	u8 *buf;

	buf = kzalloc(len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx, buf, len),
			-EMSGSIZE);
	kfree(buf);
}

/* A buffer too short to hold even one tag+length header is rejected. */
static void asn1_test_short_header(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = { 0x30 };

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/* Indefinite-length constructed form with no terminating EOC is rejected. */
static void asn1_test_missing_eoc(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x80,			/* indefinite length */
			0x02, 0x01, 0x2a,
			0x04, 0x02, 0x68, 0x69,
						/* no 0x00 0x00 EOC */
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/* An EOC marker with a non-zero length octet is rejected. */
static void asn1_test_invalid_eoc(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x80,			/* indefinite length */
			0x02, 0x01, 0x2a,
			0x04, 0x02, 0x68, 0x69,
			0x00, 0x01,		/* EOC with bad length */
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			-EBADMSG);
}

/*
 * Indefinite-length constructed form terminated by a proper end-of-contents
 * marker is accepted, and both actions fire.  This exercises the success path
 * of the indefinite-length branch (the failure paths are covered above).
 */
static void asn1_test_indefinite_ok(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x80,			/* SEQUENCE, indefinite length */
			0x02, 0x01, 0x2a,
			0x04, 0x02, 0x68, 0x69,
			0x00, 0x00,		/* end-of-contents */
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			0);
	KUNIT_EXPECT_EQ(test, ctx.int_calls, 1u);
	KUNIT_EXPECT_EQ(test, ctx.str_calls, 1u);
	KUNIT_EXPECT_EQ(test, ctx.str_len, (size_t)2);
}

/* A two-octet (0x81) long-form length is parsed the same as the short form. */
static void asn1_test_long_form_length(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x81, 0x07,		/* long-form length: 1 octet, 0x07 */
			0x02, 0x01, 0x2a,
			0x04, 0x02, 0x68, 0x69,
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			0);
	KUNIT_EXPECT_EQ(test, ctx.int_first, (u8)0x2a);
	KUNIT_EXPECT_EQ(test, ctx.str_len, (size_t)2);
}

/* A zero-length OCTET STRING is valid; the action fires with vlen == 0. */
static void asn1_test_zero_length_string(struct kunit *test)
{
	struct asn1_test_ctx ctx = {};
	static const u8 der[] = {
		0x30, 0x05,
			0x02, 0x01, 0x2a,
			0x04, 0x00,		/* OCTET STRING of length 0 */
	};

	KUNIT_EXPECT_EQ(test,
			asn1_ber_decoder(&asn1_test_decoder, &ctx,
					 der, sizeof(der)),
			0);
	KUNIT_EXPECT_EQ(test, ctx.str_calls, 1u);
	KUNIT_EXPECT_EQ(test, ctx.str_len, (size_t)0);
}

/*
 * Bounded deterministic stress.  A fixed-seed xorshift PRNG drives a few
 * thousand pseudo-random byte strings (lengths 0..255) through the decoder.
 * The decoder must never crash, and whenever it reports success the slice
 * oracle in the callbacks must not have seen an out-of-range (value, vlen).
 * On a KASAN build any over-read is caught in addition to the oracle.  The
 * seed and iteration count are fixed so the case is reproducible and runs in
 * bounded time.
 */
static void asn1_test_random_stress(struct kunit *test)
{
	static const unsigned int iterations = 4096;
	u32 state = 0x1a2b3c4du;		/* fixed seed: reproducible */
	u8 *buf;
	unsigned int i;

	buf = kmalloc(256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	for (i = 0; i < iterations; i++) {
		struct asn1_test_ctx ctx = {};
		size_t len, j;
		int ret;

		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		len = state % 256;

		for (j = 0; j < len; j++) {
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			buf[j] = state & 0xff;
		}

		ctx.buf = buf;
		ctx.buflen = len;
		ret = asn1_ber_decoder(&asn1_test_decoder, &ctx, buf, len);

		/* Any return is acceptable; a bad slice on success is not. */
		if (ret == 0)
			KUNIT_EXPECT_FALSE(test, ctx.slice_oob);
		else
			KUNIT_EXPECT_LT(test, ret, 0);
	}

	kfree(buf);
}

static struct kunit_case asn1_decoder_test_cases[] = {
	KUNIT_CASE(asn1_test_decode_valid),
	KUNIT_CASE(asn1_test_truncated_content),
	KUNIT_CASE(asn1_test_length_overrun),
	KUNIT_CASE(asn1_test_long_tag),
	KUNIT_CASE(asn1_test_wrong_tag),
	KUNIT_CASE(asn1_test_too_large),
	KUNIT_CASE(asn1_test_short_header),
	KUNIT_CASE(asn1_test_missing_eoc),
	KUNIT_CASE(asn1_test_invalid_eoc),
	KUNIT_CASE(asn1_test_indefinite_ok),
	KUNIT_CASE(asn1_test_long_form_length),
	KUNIT_CASE(asn1_test_zero_length_string),
	KUNIT_CASE(asn1_test_random_stress),
	{}
};

static struct kunit_suite asn1_decoder_test_suite = {
	.name = "asn1_decoder",
	.test_cases = asn1_decoder_test_cases,
};

kunit_test_suite(asn1_decoder_test_suite);

MODULE_DESCRIPTION("KUnit tests for the ASN.1 BER/DER decoder");
MODULE_LICENSE("GPL");
