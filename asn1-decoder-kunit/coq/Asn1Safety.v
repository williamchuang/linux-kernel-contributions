(*
  Asn1Safety.v
  ---------------------------------------------------------------------------
  Coq port of Asn1Safety.lean: an abstract length-accounting model of
  lib/asn1_decoder.c (the kernel BER/DER TLV decoder, asn1_ber_decoder), with
  machine-checked proofs that the bounds checks in the source guarantee, for
  every input and every execution path:

    (I1) no over-read   -- the read cursor never passes the buffer end;
    (I2) slice validity -- every (value, vlen) slice handed to an action
         callback lies completely inside the buffer.

  A second proof assistant, independent of the Lean development, checking the
  same model.  This file compiles with coqc (Coq 8.18); the companion
  model_check.py gives executable backing and the Lean file a third check.

  Author: William Chuang.  Assisted-by: Claude Opus 4.8.
*)

Require Import Lia.
Require Import List.
Import ListNotations.

(** Decoder state: read cursor [dp], innermost container end [cur] (the C
    local [datalen]), total buffer length [buf], and the stack of enclosing
    container ends [stk] (the C [cons_datalen_stack]). *)
Record St : Type := mkSt {
  dp  : nat;
  cur : nat;
  buf : nat;
  stk : list nat
}.

(** Enclosing container ends are non-decreasing from the inner floor [lo] up to
    [b]: [lo <= e0 <= e1 <= ... <= b].  This monotonicity is what nesting
    enforces and what re-establishes [dp <= cur] when a container is left. *)
Fixpoint StkWf (b : nat) (lo : nat) (l : list nat) : Prop :=
  match l with
  | []        => True
  | e :: rest => lo <= e /\ e <= b /\ StkWf b e rest
  end.

(** The loop invariant maintained across every decode step. *)
Definition Wf (s : St) : Prop :=
  dp s <= cur s /\ cur s <= buf s /\ StkWf (buf s) (cur s) (stk s).

Definition ReadOk  (s : St) (k : nat)       : Prop := dp s + k   <= buf s.
Definition SliceOk (s : St) (off len : nat) : Prop := off  + len <= buf s.

(** Operations; each carries the guard the C code checks as a hypothesis. *)
Definition readHeader (s : St) (hdr : nat) : St :=
  mkSt (dp s + hdr) (cur s) (buf s) (stk s).
Definition enterCons (s : St) (len : nat) : St :=
  mkSt (dp s) (dp s + len) (buf s) (cur s :: stk s).
Definition emitPrim (s : St) (len : nat) : St :=
  mkSt (dp s + len) (cur s) (buf s) (stk s).
Definition leaveCons (s : St) (outer : nat) (rest : list nat) : St :=
  mkSt (cur s) outer (buf s) rest.

(** (I1)/(I2): guard-satisfying reads and slices are in-bounds. *)

Theorem header_readOk : forall s hdr,
  Wf s -> dp s + hdr <= cur s -> ReadOk s hdr.
Proof. intros s hdr [H1 [H2 H3]] g. unfold ReadOk. lia. Qed.

Theorem prim_sliceOk : forall s len,
  Wf s -> dp s + len <= cur s -> SliceOk s (dp s) len.
Proof. intros s len [H1 [H2 H3]] g. unfold SliceOk. lia. Qed.

(** Preservation: every operation keeps the invariant [Wf]. *)

Theorem readHeader_pres : forall s hdr,
  Wf s -> dp s + hdr <= cur s -> Wf (readHeader s hdr).
Proof.
  intros s hdr H g. destruct H as [H1 [H2 H3]].
  unfold Wf, readHeader; simpl.
  split; [lia | split; [lia | exact H3]].
Qed.

Theorem emitPrim_pres : forall s len,
  Wf s -> dp s + len <= cur s -> Wf (emitPrim s len).
Proof.
  intros s len H g. destruct H as [H1 [H2 H3]].
  unfold Wf, emitPrim; simpl.
  split; [lia | split; [lia | exact H3]].
Qed.

Theorem enterCons_pres : forall s len,
  Wf s -> dp s + len <= cur s -> Wf (enterCons s len).
Proof.
  intros s len H g. destruct H as [H1 [H2 H3]].
  unfold Wf, enterCons; simpl.
  split; [lia | split; [lia | ]].
  simpl. split; [lia | split; [lia | exact H3]].
Qed.

(** Leaving a container restores the pushed end [outer].  The stack invariant
    supplies [cur <= outer], [outer <= buf], and well-formedness of the
    remaining stack from floor [outer] -- exactly what re-establishes
    [dp <= cur] after the cursor jumps to the old container end. *)
Theorem leaveCons_pres : forall s outer rest,
  Wf s -> stk s = outer :: rest -> Wf (leaveCons s outer rest).
Proof.
  intros s outer rest H hstk. destruct H as [H1 [H2 H3]].
  rewrite hstk in H3. simpl in H3. destruct H3 as [Hco [Hob Hrest]].
  unfold Wf, leaveCons; simpl.
  split; [lia | split; [lia | exact Hrest]].
Qed.

(** Top-level: the entry state [dp = 0], [cur = buf], empty stack -- how
    asn1_ber_decoder begins -- satisfies the invariant. *)
Definition initState (b : nat) : St := mkSt 0 b b [].

Theorem init_wf : forall b, Wf (initState b).
Proof. intro b. unfold Wf, initState; simpl. split; [lia | split; [lia | exact I]]. Qed.
