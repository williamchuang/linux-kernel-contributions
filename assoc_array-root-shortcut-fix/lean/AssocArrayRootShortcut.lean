/-
  Lean 4 (core, no Mathlib) formalization of the assoc_array root-shortcut-split
  bug.  Models the essence of the defect — the level at which a node is indexed
  during a shortcut split — and proves, by kernel computation, that the buggy
  split makes a stored key unfindable while the fixed split, which keeps a
  pre-shortcut covering the skipped leading segment, restores it.

  Scope: the theorems concern the abstract model below, not lib/assoc_array.c.
  The model drops pointer tagging, allocation and RCU; keys are segment lists and
  the tree is an inductive type.  Bounded verification of the real C is the
  exhaustive enumeration of the ported code.

  Check with Lean 4 (v4.15.0):   lean AssocArrayRootShortcut.lean
  The theorems close by `decide`; `native_decide` is an alternative.
-/

namespace AssocArrayRootShortcut

abbrev Seg := Nat          -- a key segment (fan-out 4 here: values 0..3)
abbrev Key := List Seg     -- a key is its sequence of segments, level 0 first

/-- The tree indexes keys one segment per level.  `shortcut n t` skips `n`
    segments (a span with no branching) and then continues at `t`. -/
inductive Tree where
  | leaf     : Key → Tree
  | node     : List (Seg × Tree) → Tree
  | shortcut : Nat → Tree → Tree

open Tree

/-- Look up the child a node branches to for segment `k`. -/
def childAt : List (Seg × Tree) → Seg → Option Tree
  | [],            _ => none
  | (s, t) :: rest, k => if s == k then some t else childAt rest k

/-- Walk the remaining key segments through the tree (fuel bounds the depth and
    makes termination trivial; any bound ≥ tree depth is enough). -/
def findAux : Nat → Tree → Key → Key → Bool
  | 0,        _,              _,   _          => false
  | _ + 1,    leaf stored,    key, _          => stored == key
  | fuel + 1, node cs,        key, s :: rest  =>
      match childAt cs s with
      | some t => findAux fuel t key rest
      | none   => false
  | _ + 1,    node _,         _,   []         => false
  | fuel + 1, shortcut n child, key, rem      => findAux fuel child key (rem.drop n)

/-- Is `key` reachable in `t`? -/
def find (t : Tree) (key : Key) : Bool := findAux 100 t key key

/- ------------------------------------------------------------------ -/
/-  The split operation, modelled so buggy and fixed differ in exactly  -/
/-  one thing: whether the pre-shortcut covering the skipped leading    -/
/-  segment(s) is kept.                                                 -/
/- ------------------------------------------------------------------ -/

/-- The node produced by the split: the old cluster under its segment, and the
    new leaf under its own segment. -/
def splitNode (oldSeg : Seg) (cluster : Tree) (newSeg : Seg) (newKey : Key) : Tree :=
  node [(oldSeg, cluster), (newSeg, leaf newKey)]

/-- Fixed split: keep a pre-shortcut of length `d` so the new node is reached at
    level `d`, exactly as the leading segments require. -/
def fixedSplit (d : Nat) (oldSeg : Seg) (cluster : Tree) (newSeg : Seg) (newKey : Key) : Tree :=
  shortcut d (splitNode oldSeg cluster newSeg newKey)

/-- Buggy split (the root case in assoc_array_insert_mid_shortcut): the same
    node, but installed with no pre-shortcut, so it is walked `d` segments too
    shallow. -/
def buggySplit (_d : Nat) (oldSeg : Seg) (cluster : Tree) (newSeg : Seg) (newKey : Key) : Tree :=
  splitNode oldSeg cluster newSeg newKey

/-- The buggy result is precisely the fixed result with its pre-shortcut
    stripped off: the omission of that `shortcut d` is the whole bug. -/
theorem buggy_is_fixed_without_preshortcut
    (d oldSeg newSeg : Nat) (cluster : Tree) (newKey : Key) :
    buggySplit d oldSeg cluster newSeg newKey
      = (match fixedSplit d oldSeg cluster newSeg newKey with
         | shortcut _ t => t
         | t => t) := rfl

/- ------------------------------------------------------------------ -/
/-  Concrete scenario (fan-out 4).                                      -/
/-  Three keys share segment 0 = 0.  k0,k1 also share segment 1 = 0 and -/
/-  differ at segment 2 (they form a cluster reached past a shortcut).  -/
/-  kd diverges at segment 1, which splits the root shortcut at d = 1.  -/
/- ------------------------------------------------------------------ -/

def k0 : Key := [0, 0, 0]
def k1 : Key := [0, 0, 1]
def kd : Key := [0, 1, 0]

/-- The cluster {k0, k1}: after segments 0 and 1 are consumed, they are told
    apart by segment 2, so this node branches on segment 2. -/
def cluster : Tree := node [(0, leaf k0), (1, leaf k1)]

/-- diverge level d = 1, old branch under segment 1 = 0, new leaf under 1. -/
def fixedTree : Tree := fixedSplit 1 0 cluster 1 kd
def buggyTree : Tree := buggySplit 1 0 cluster 1 kd

/- ------------------------------------------------------------------ -/
/-  Results, decided by the kernel.                                     -/
/- ------------------------------------------------------------------ -/

/-- The fixed split keeps every key findable. -/
theorem fixed_finds_all :
    find fixedTree k0 = true ∧ find fixedTree k1 = true ∧ find fixedTree kd = true := by
  decide

/-- The buggy split loses k1 and kd: they are not findable. -/
theorem buggy_loses_keys :
    find buggyTree k1 = false ∧ find buggyTree kd = false := by
  decide

/-- The crux, in one statement: the same stored key is unfindable after the
    buggy split and findable after the fix. -/
theorem bug_and_fix :
    find buggyTree k1 = false ∧ find fixedTree k1 = true := by
  decide

end AssocArrayRootShortcut
