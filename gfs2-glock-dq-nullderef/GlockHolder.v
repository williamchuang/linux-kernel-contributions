(*
  GlockHolder.v
  ---------------------------------------------------------------------------
  A minimal machine-checked model of the control-flow property fixed by
  "gfs2: Fix NULL pointer dereference in gfs2_seek_data/gfs2_seek_hole".

  gfs2_glock_nq_init() either acquires the holder (success) or, on failure,
  calls gfs2_holder_uninit() which sets gh.gh_gl = NULL.  gfs2_glock_dq_uninit()
  dereferences gh_gl (it takes gh->gh_gl->gl_lockref.lock), so it is safe only
  on an acquired holder.

  The property: the dequeue is reached only when the holder was acquired.
  The buggy code dequeues unconditionally (crash on the failure path); the
  fixed code (Andreas Gruenbacher's `goto out`) dequeues iff nq_init succeeded.

  This models the control flow, not the C pointer semantics; it states exactly
  what the one-line fix guarantees and what the bug violated.

  Toolchain: Coq 8.18 (coqc), core only.
  Author: William Chuang.  Assisted-by: Claude Opus 4.8.
*)

(** A holder is either acquired (gh_gl valid) or uninitialised (gh_gl = NULL). *)
Inductive Holder := Acquired | Uninit.

(** gfs2_glock_nq_init: success acquires; failure leaves the holder uninit. *)
Definition nq_init (success : bool) : Holder :=
  if success then Acquired else Uninit.

(** gfs2_glock_dq_uninit dereferences gh_gl; safe only on an acquired holder. *)
Definition dq_safe (h : Holder) : Prop := h = Acquired.

(** Buggy control flow: dequeue is reached unconditionally. *)
Definition buggy_calls_dq (success : bool) : bool := true.

(** Fixed control flow (`if (ret) goto out;`): dequeue reached iff nq succeeded. *)
Definition fixed_calls_dq (success : bool) : bool := success.

(** The fix is correct: every dequeue the fixed flow performs is on an
    acquired holder, so gh_gl is never NULL at gfs2_glock_dq_uninit. *)
Theorem fixed_is_safe : forall success,
  fixed_calls_dq success = true -> dq_safe (nq_init success).
Proof.
  intros success H. unfold fixed_calls_dq in H. subst success.
  unfold dq_safe, nq_init. reflexivity.
Qed.

(** The bug is real: on the failure path the buggy flow dequeues an
    uninitialised holder -- the NULL-pointer dereference syzbot reported. *)
Theorem buggy_is_unsafe : exists success,
  buggy_calls_dq success = true /\ ~ dq_safe (nq_init success).
Proof.
  exists false. split.
  - reflexivity.
  - unfold dq_safe, nq_init. discriminate.
Qed.
