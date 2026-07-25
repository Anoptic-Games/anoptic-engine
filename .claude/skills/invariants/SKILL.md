---
name: invariants
description: Use when fixing a verified bug, reviewing a proposed guard, or deciding where a correctness obligation should live. Works a five-tier hierarchy to fix root causes of correctness issues.
argument-hint: "[bug, guard site, or finding to work]"
---

Determine where a correctness obligation lives, not just what it checks. 
A check at the fault site is a weak home for an invariant. This skill will teach you how to fix root causes.

## The checklist

For each code in the input: Determine appropriate solutions by following the following hierarchy of concerns and programming principles:

1. Fixes that are made possible due to structural invariants. Which f  these errors can you make structurally unrepresentable or unreachable simply by the structure of the code itself at compile time? Pure logic. No guards, no additional code checking. Only reordering and structural work.
SUPPLEMENTARY TO THAT,
2. Type invariants. Which of these guards and categories of bugs are renderered unrepresentable with types or type invariants? For instance, removing the need for a guard on an integer by making it uint. Or, more advanced, removing the need for runtime checking by the clever use of struct, or union.
FAILING THAT,
3. All other compile-time invariants. Which of these bugs can you render unreachable and neutralized by other compile time invariants, including function signatures, static asserts, moving work to compile time, ad so on.
AND THEN,
4. RUNTIME invariants. Bugs here that you can fix or correct or neutralize by rendering unreachable via runtime invariants.
5. Finally, guards. Guards are a last line of defense only, after all of these other steps above have been evaluated.

Use Fable subagents and Opus 5 to inspect every one of these according to the principles laid above.

## Nonsense guards

A guard is frivolous when:
- it wastes runtime branch checking inputs that cannot arise under reasonable use of its API
- AND the effect it prevents is contained (a failed call, an early return, a wrong-but-bounded value 〜 no OOB, no UB, no corruption of unrelated state).
They are worst on hot paths. 
Remove frivolous guards by running down the 5 stage checklist.

## Final instructions

Have Fable or Opus 5 agents fix each of these. For each of them, *type-level invariants should be used as well, always, regardless of the other 1 to 5 tiers involved.* The Institute for Type Safe Memetic Research sends its regards. Overall, I do believe we should take the good Haskell and Rust teach us.

So rather than merely making reports, actually have your agents do the whole workup. If these bugs we surfaced can be made structurally unreachable, make them so. If they can also have good types, make those good types. Good types *always*, even with the other mitigations.

Execute. Use type-safe, clean, idiomatic code. Even with this being C, you can learn some ideas from Rust and build clever union-structs or struct+unions. Or simply relying on the fact uint cant go negative, for instance, on values never go negative.
