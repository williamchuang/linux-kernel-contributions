#!/usr/bin/env python3
"""
Executable mirror of Asn1Safety.lean.  Runs the *same* abstract length-
accounting machine and checks, by exhaustive and randomized execution, that
every theorem the Lean file states actually holds on the model:

  - header_readOk / prim_sliceOk : guard-satisfying reads & slices are in-bounds
  - *_pres                        : every operation preserves Wf
  - init_wf                       : the entry state is Wf

This does not replace the Lean proof (which quantifies over ALL states); it is
a differential check that the model encoded in Lean is internally correct, so a
modeling slip would surface here even without the Lean toolchain.
"""
import itertools, random

class St:
    __slots__=("dp","cur","buf","stk")
    def __init__(s,dp,cur,buf,stk): s.dp,s.cur,s.buf,s.stk=dp,cur,buf,list(stk)

def StkWf(lo,buf,stk):
    # each enclosing end >= the inner floor and <= buf; floor rises with depth
    for e in stk:
        if not (lo<=e<=buf): return False
        lo=e
    return True
def Wf(s):
    return s.dp<=s.cur and s.cur<=s.buf and StkWf(s.cur,s.buf,s.stk)

# operations (mirror the Lean defs)
def readHeader(s,h): return St(s.dp+h, s.cur, s.buf, s.stk)
def enterCons(s,l):  return St(s.dp, s.dp+l, s.buf, [s.cur]+s.stk)
def emitPrim(s,l):   return St(s.dp+l, s.cur, s.buf, s.stk)
def leaveCons(s):    return St(s.cur, s.stk[0], s.buf, s.stk[1:])

viol=0
def check(cond,msg):
    global viol
    if not cond:
        viol+=1; print("VIOLATION:",msg)

# ---- 1. Exhaustive small-state sweep of every operation + its guard ----
# enumerate all states with small numeric fields and short stacks
RANGE=range(0,7)
def gen_states():
    for buf in RANGE:
        for cur in range(0,buf+1):
            for dp in range(0,cur+1):
                # stacks: [] and all 1-2 element stacks with cur<=e<=buf
                yield St(dp,cur,buf,[])
                for e1 in range(cur,buf+1):
                    yield St(dp,cur,buf,[e1])
                    for e2 in range(cur,buf+1):
                        yield St(dp,cur,buf,[e1,e2])

n_states=0; n_trans=0
for s in gen_states():
    if not Wf(s):  # only start from Wf states (the invariant's domain)
        continue
    n_states+=1
    # readHeader for every hdr with guard dp+hdr<=cur
    for hdr in range(0, s.cur-s.dp+1):
        check(s.dp+hdr<=s.buf, f"header read oob {s.dp}+{hdr}>{s.buf}")   # I1
        t=readHeader(s,hdr); check(Wf(t),"readHeader !Wf"); n_trans+=1
    # emitPrim / enterCons for every len with guard dp+len<=cur
    for ln in range(0, s.cur-s.dp+1):
        check(s.dp+ln<=s.buf, f"slice oob {s.dp}+{ln}>{s.buf}")           # I2
        t=emitPrim(s,ln);  check(Wf(t),"emitPrim !Wf");  n_trans+=1
        t=enterCons(s,ln); check(Wf(t),"enterCons !Wf"); n_trans+=1
    # leaveCons when stack nonempty
    if s.stk:
        t=leaveCons(s); check(Wf(t),"leaveCons !Wf"); n_trans+=1

print(f"[exhaustive] Wf start-states={n_states}  guarded transitions checked={n_trans}  violations={viol}")

# ---- 2. Randomized deep executions: long guard-respecting op sequences ----
random.seed(0xA5A1)
rng_steps=0
for it in range(200_000):
    buf=random.randint(0,300)
    s=St(0,buf,buf,[])   # initState
    for _ in range(random.randint(0,40)):
        if not Wf(s): check(False,"invariant broke mid-run"); break
        choices=[]
        if s.cur-s.dp>=2: choices.append("hdr")
        if s.cur-s.dp>=0: choices+=["prim","cons"]
        if s.stk: choices.append("leave")
        op=random.choice(choices)
        if op=="hdr":
            h=random.randint(2,s.cur-s.dp); check(s.dp+h<=s.buf,"rnd hdr oob"); s=readHeader(s,h)
        elif op=="prim":
            l=random.randint(0,s.cur-s.dp); check(s.dp+l<=s.buf,"rnd slice oob"); s=emitPrim(s,l)
        elif op=="cons":
            l=random.randint(0,s.cur-s.dp); check(s.dp+l<=s.buf,"rnd cons slice oob"); s=enterCons(s,l)
        else:
            s=leaveCons(s)
        check(Wf(s),"rnd !Wf"); rng_steps+=1

print(f"[random]     deep runs=200,000  steps={rng_steps}  violations={viol}")
print("MODEL-CHECK:", "PASS (Lean model is internally consistent)" if viol==0 else f"FAIL ({viol})")
raise SystemExit(1 if viol else 0)
