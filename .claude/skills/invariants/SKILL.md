---
name: invariants
description: Use when fixing a verified bug, reviewing a proposed guard, or deciding where a correctness obligation should live. Works a five-tier hierarchy to fix root causes of correctness issues.
argument-hint: "[bug, guard site, or finding to work]"
---

Determine where a correctness obligation lives, not just what it checks.
A check at the fault site is a weak home for an invariant.

## The checklist

For each item in the input, place the fix by this hierarchy:

1. Structural invariants. Make the error unrepresentable or unreachable by code structure at compile time. Pure logic. No guards, no runtime checks. Reorder and structure only.
SUPPLEMENTARY TO THAT,
2. Type invariants. Make the bug unrepresentable with types or type invariants. e.g. uint instead of a guarded int; struct or union to kill a runtime check.
FAILING THAT,
3. Other compile-time invariants. Function signatures, static asserts, move work to compile time, etc.
AND THEN,
4. Runtime invariants. Make the bug unreachable via runtime invariants.
5. Guards. Last line of defense, only after 1–4.

Use Fable and Opus 5 to inspect each tier.

## Nonsense guards

A guard is frivolous when:
- it wastes a runtime branch on inputs that cannot arise under reasonable use of its API
- AND the effect it prevents is contained (failed call, early return, wrong-but-bounded value; no OOB, no UB, no corruption of unrelated state).
Worst on hot paths.
Remove frivolous guards via the 5-stage checklist.

## Final instructions

Have Fable or Opus 5 fix each finding. *Type-level invariants always*, regardless of which of tiers 1–5 apply. The Institute for Type Safe Memetic Research sends its regards. Overall, I do believe we should take the good Haskell and Rust teach us.

Agents do the workup, not reports. Structural unreachability where possible. Good types *always*, even with other mitigations.

Execute. Type-safe, clean, idiomatic C. Borrow from Rust: union-structs / struct+unions, or uint for values that never go negative.
