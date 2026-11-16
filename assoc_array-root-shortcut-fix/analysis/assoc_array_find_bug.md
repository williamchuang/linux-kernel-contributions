# assoc_array_find() loses lookups after a root-shortcut split

On Linux 7.4-rc3. Data is never lost; only the lookup path is affected.

## Symptom

Insert 18 keys through the public `assoc_array` API:

- keys 0–16: first nibble = 0, second nibble = 0, distinguished by higher nibbles
  (so they share an 8-bit prefix and cluster behind one shortcut);
- key 17: first nibble = 0, second nibble = 1 (diverges one nibble earlier than
  the shortcut's end).

After the 18th insert:

| Measure | Value | Meaning |
| --- | --- | --- |
| `assoc_array_iterate` count | 18 | every object is still in the tree |
| `nr_leaves_on_tree` | 18 | leaf accounting is correct |
| keys returned correctly by `assoc_array_find` | 2 of 18 | the lookup path is broken |

The keys are pairwise distinct (asserted in the reproducer), so the cause is not
duplicate keys, freed objects, or a model error.

## Mechanism

The clustered keys first build a root shortcut that skips levels 0–7 (covering
the shared nibble 0 and nibble 1) and points at a node at level 8.

Inserting key 17 diverges at level 4, so `assoc_array_insert_mid_shortcut()`
splits the shortcut at `diff = 4`:

```
	level += ASSOC_ARRAY_LEVEL_STEP;	/* level becomes 4 */
	if (diff > level) {			/* 4 > 4 is false */
		... create a pre-shortcut covering the skipped leading bits ...
	} else {
		... attach the new node directly to the shortcut's parent ...
	}
```

For a non-root shortcut this is correct: the parent node already consumed the
entering nibble, so the new node lands at the right level. For a root shortcut
(`back_pointer == NULL`) nothing above consumes nibble 0, yet the `else` branch
installs the new node as the root. A root node is walked at level 0, yet this
node is indexed for level 4. Every node beneath it is then walked 4 bits too
shallow.

The walk trace for a missing key shows nodes visited at levels 0, 4, 8 in a tree
whose nodes were built for levels 4, 8, 12:

```
walk: enter shortcut level=0 skip_to=8
walk: WRONG shortcut sc_level=4 dis=10
split: level=0 sc_level=4 dis=10 -> diff=4 skip_to=8   (no pre-shortcut taken)
find: node level=0 slot=0 -> node        (built for level 4)
find: node level=4 slot=0 -> node        (built for level 8)
find: node level=8 slot=1 -> leaf/empty  -> not found
```

## Confirmation

Forcing the pre-shortcut to cover the leading nibble in the root case makes
`assoc_array_find` return the correct object for all 18 keys, and resolves every
case in the bounded enumeration.

## Reach

The code dates to 2013 (commit 3cb989501c26) and sits under every keyring. The
documentation recommends that index keys begin with a hash, and keyring keys do,
which scatters them and makes a 2-nibble shared low prefix with an
earlier-diverging sibling essentially impossible in production. The same
documentation states that poor scattering must still work correctly, only more
slowly — so by the documented contract this is a correctness defect, reachable
with poorly-scattered keys.

## Reproducer

`assoc_array_find_bug_repro.c` — a KUnit case that inserts the 18 keys, asserts
they are distinct, and reports the iterate/leaf/find counts. On the clean tree it
reports findable = 2 of 18.
