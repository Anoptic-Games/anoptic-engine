---
name: invariants
description: Use when fixing a verified bug, reviewing a proposed guard, or deciding where a correctness obligation should live. Works a five-tier hierarchy top-down 〜 structure, types, other compile time, runtime seams, guards last 〜 and layers type-level welds onto every fix regardless of which tier wins.
argument-hint: "[bug, guard site, or finding to work]"
---

Determine where a correctness obligation lives, not just what it checks. A check at the fault site
is the weakest possible home for an invariant; most defects admit a stronger one. Work the tiers
strictly top-down and take the highest that fully neutralizes the defect. For every tier you reject,
write one line saying why it cannot 〜 that line is the review artifact.

## Gate: verify before you fix

No fix lands on an unverified claim. Confirm the defect with file:line evidence, the exact
expressions, and the minimal trigger 〜 or refute it. Findings from surveys and reports are
hypotheses; this codebase has refuted plausible-sounding OOB claims (a uint8_t funnel upstream made
the index total) and has had fixes that repaired the wrong polarity of the right branch. A fix for
a misdiagnosed bug is a new bug with better morale.

## The hierarchy

1. STRUCTURAL 〜 pure logic and reordering make the bad state unreachable by construction. No new
checks, no new code paths. Shapes: put the read on the branch that performed the write; take the
bound and the array from the same call so they cannot disagree; make one function the sole decode
point so the second unchecked use ceases to exist; derive a value from the field that already
carries the guarantee instead of a field that can drift. Deletion counts: if a failure branch is
unreachable by platform contract, the structural fix is removing it, loudly validating once at init.

2. TYPE 〜 the bad state becomes unrepresentable. Shapes: unsigned for values that cannot be
negative (only after grepping every use for sign-dependence 〜 unsigning a value used in signed
modular arithmetic silently changes results); delete the field that can disagree with the
allocation it describes; a proof-token struct minted only at a gate, required by everything
downstream, zero-cost at the ABI; wrapper structs where two same-typed quantities differ in unit or
validity. Know C's limits honestly: no range types (no integer type caps at 32), invariants do not
survive a memcpy seam, and field-wise assignment bypasses any constructor. When a wrapper cannot
survive transport or would churn a parity-locked module, decline it in one line and weld with tier 3.

3. OTHER COMPILE TIME 〜 static_assert, signatures, moving work to constants. This tier rarely wins
alone but always accompanies: weld table extents to the bounds that index them, cap expressions to
the reserves they must cover, ABI masks to the properties the decode assumes (disjoint and total in
one XOR), field types to the comparison semantics a predicate depends on (_Generic). A weld turns
"two call sites remembered" into "the compiler remembers."

4. RUNTIME INVARIANT 〜 establish the property once at a seam; everything downstream inherits it.
Shapes: clamp or validate at the ingress that dominates every consumer in the call graph (find ALL
ingresses 〜 public API and internal test paths); re-establish the invariant before each iteration
of a loop rather than checking at the fault site inside it; a single validity predicate at the one
seam where the value enters, leaving the hot path branch-free. One seam, not N checks.

5. GUARD at the fault site 〜 last resort, and only after tiers 1-4 each have their rejection line.
For a real seam (untrusted input, memory damage) some check must exist SOMEWHERE 〜 the hierarchy
decides where, never whether. Scattered per-site guards are the outcome the hierarchy exists to
avoid.

## Good types always

Whatever tier wins, layer the type work on top. Every fix carries its weld: the static_asserts, the
narrowed or unsigned domains, the deleted degrees of freedom. A tier-4 clamp without a tier-3 weld
is two facts that can drift apart; with the weld it is one fact.

Float predicates are type discipline too: write comparisons in accept-form (`if (!(x > lo))`) so
NaN and inf fall to the reject branch by IEEE semantics instead of slipping through both clamps.
Totality over the full bit pattern of the type, not just its intended domain.

## Nonsense guards

A guard is frivolous when both hold: it rejects inputs that cannot arise under reasonable use of
its API, AND the effect it prevents is contained (a failed call, an early return, a wrong-but-
bounded value 〜 no OOB, no UB, no corruption of unrelated state). Either condition alone acquits.
Remove frivolous guards; they are worst on hot paths. A sentinel check that tests for a value that
cannot arrive (because the sentinel does not survive an arithmetic conversion, or its producer is
unreachable) is worse than no check 〜 it documents protection that does not exist.

## Diagnostics

- Platform divergence: a guard present in one sibling and absent in the others, where the others
  are not buggy, marks an arithmetic artifact, not a domain constraint. Real constraints appear in
  every sibling or in the header.
- Sibling asymmetry: a guarded decrement beside an unguarded increment of the same counter proves
  invariant distrust, not an invariant.
- Once a defect shape has a name, grep it across the whole tree 〜 per-module readers miss repeat
  instances of a pattern they just described.
- Two sources of truth for one quantity (a count checked here, an array indexed there, filled at
  different times) is its own defect even while the staleness happens to shield something worse.

## Obligations

- Negative control: run the trigger against the pre-fix code and watch it fail the way the
  diagnosis says (sanitizer signature, failing check, wrong output). A fix verified only by the
  suite passing is inference, not evidence.
- Bit-exactness is measured, not argued: where output parity is contracted (GPU mirrors, oracle
  digests), diff the full output pre/post 〜 a dump and cmp, not a prose argument about evaluation
  order.
- Every landed change gets a forensic record: what was wrong, the tier chosen, the rejection lines,
  the behaviour delta, the test that pins it. Refutations are recorded where the claim lived.
- A new invariant gets a guard test with controls first: cases a reject-everything fix fails,
  then the triggers. A test that only checks rejection certifies the wrong fix.
