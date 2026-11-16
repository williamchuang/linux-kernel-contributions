// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal reproducer: assoc_array_find() loses lookups after a root-shortcut
 * split with poorly-scattered keys.  The objects stay in the tree (iteration
 * and the leaf count are correct) but assoc_array_find() cannot navigate to
 * most of them.
 *
 * Build: place in lib/tests/, add to lib/tests/Makefile under a Kconfig that
 * selects KUNIT and ASSOCIATIVE_ARRAY, then:
 *   ./tools/testing/kunit/kunit.py run aa_find_bug
 */
#include <kunit/test.h>
#include <linux/assoc_array.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#define KEY_WORDS 3

struct key3 { unsigned long w[KEY_WORDS]; };

struct obj {
	struct key3 key;
	unsigned int idx;
};

static unsigned long chunk(const struct key3 *k, int level)
{
	int i = level / BITS_PER_LONG;

	return i < KEY_WORDS ? k->w[i] : 0;
}

static unsigned long get_key_chunk(const void *index_key, int level)
{
	return chunk(index_key, level);
}

static unsigned long get_object_key_chunk(const void *object, int level)
{
	return chunk(&((const struct obj *)object)->key, level);
}

static bool compare_object(const void *object, const void *index_key)
{
	return !memcmp(&((const struct obj *)object)->key, index_key,
		       sizeof(struct key3));
}

static int diff_objects(const void *object, const void *index_key)
{
	const struct key3 *a = &((const struct obj *)object)->key;
	const struct key3 *b = index_key;
	int i;

	for (i = 0; i < KEY_WORDS; i++) {
		unsigned long x = a->w[i] ^ b->w[i];

		if (x)
			return i * BITS_PER_LONG + __ffs(x);
	}
	return -1;
}

static void free_object(void *object)
{
	kfree(object);
}

static const struct assoc_array_ops ops = {
	.get_key_chunk		= get_key_chunk,
	.get_object_key_chunk	= get_object_key_chunk,
	.compare_object		= compare_object,
	.diff_objects		= diff_objects,
	.free_object		= free_object,
};

static void set_nibble(struct key3 *k, int bit, unsigned long v)
{
	int w = bit / BITS_PER_LONG, s = bit % BITS_PER_LONG;

	k->w[w] &= ~(0xfUL << s);
	k->w[w] |= (v & 0xf) << s;
}

static int count_iter(const void *object, void *data)
{
	(*(unsigned int *)data)++;
	return 0;
}

static void aa_find_bug(struct kunit *test)
{
	struct assoc_array array;
	struct obj *objs[18];
	struct key3 keys[18];
	unsigned int i, j, iter = 0, findable = 0;

	assoc_array_init(&array);

	for (i = 0; i < 18; i++) {
		memset(&keys[i], 0, sizeof(keys[i]));
		set_nibble(&keys[i], 4, i < 17 ? 0 : 1);	/* nibble 1 */
		set_nibble(&keys[i], 8, i & 0xf);		/* nibble 2 */
		set_nibble(&keys[i], 12, (i >> 4) & 0xf);	/* nibble 3 */
		keys[i].w[KEY_WORDS - 1] = 0x100 + i;		/* globally unique */
	}
	for (i = 0; i < 18; i++)
		for (j = i + 1; j < 18; j++)
			KUNIT_ASSERT_NE_MSG(test, 0,
				memcmp(&keys[i], &keys[j], sizeof(struct key3)),
				"keys %u and %u are identical", i, j);

	for (i = 0; i < 18; i++) {
		struct assoc_array_edit *edit;

		objs[i] = kzalloc(sizeof(*objs[i]), GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, objs[i]);
		objs[i]->key = keys[i];
		objs[i]->idx = i;
		edit = assoc_array_insert(&array, &ops, &objs[i]->key, objs[i]);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, edit);
		assoc_array_apply_edit(edit);
	}

	assoc_array_iterate(&array, count_iter, &iter);
	for (i = 0; i < 18; i++)
		if (assoc_array_find(&array, &ops, &keys[i]) == objs[i])
			findable++;

	kunit_info(test, "iterate=%u nr_leaves=%lu findable=%u (all should be 18)",
		   iter, array.nr_leaves_on_tree, findable);

	/* Data present (iterate + nr_leaves) but unreachable by find(). */
	KUNIT_EXPECT_EQ(test, iter, 18);
	KUNIT_EXPECT_EQ(test, array.nr_leaves_on_tree, 18UL);
	KUNIT_EXPECT_EQ_MSG(test, findable, 18U,
			    "assoc_array_find() cannot reach every stored key");

	assoc_array_destroy(&array, &ops);
}

static struct kunit_case aa_find_bug_cases[] = {
	KUNIT_CASE(aa_find_bug),
	{}
};

static struct kunit_suite aa_find_bug_suite = {
	.name = "aa_find_bug",
	.test_cases = aa_find_bug_cases,
};
kunit_test_suite(aa_find_bug_suite);

MODULE_DESCRIPTION("assoc_array_find root-shortcut-split lookup bug reproducer");
MODULE_LICENSE("GPL");
