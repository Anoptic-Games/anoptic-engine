---
name: refine
description: Cut actual lines of code from ONE feature-complete file or module by folding repeated shapes into static inline helpers and removing redundant steps, under a byte-exact ouput lock.
argument-hint: "[one file or module]"
---

Reduce the aLoC of $ARGUMENTS in absolute terms: improve the logic, fold things into static inline
functions, cut away unnecessary or now-redundant steps. Behaviour does not change. aLoC is actual
lines of code.

## Gate

One file or one module per pass.
The target is already feature-complete and passing.

## Lock first, edit second

- Deterministic driver over a matrix of inputs: sizes, flags, both sides of every branch the module exposes. Hash everything it produces: return values, out-params, buffers written.
- Run it against the unmodified file. That hash is the lock.
- Re-run after every step. A changed hash means the fold was wrong.
- Driver lives in the scratchpad. If the module has no comparable test of its own, say so and offer to keep it.

## Survey before cutting

Read the whole file, map its functions, count occurrences of each repeated shape.

Highest yield first: duplicated blocks, hand-expanded component math, a probe or hash rewritten per operation, offset chains where each line names its predecessor.

## Bit-exactness

Preserve operation order in float code. `a/len` is not `a*(1.0f/len)`. `sqrtf(dot)` against an epsilon is not `dot` against that epsilon squared. Helpers return the raw quantity and leave the comparison at the call site.

## The pass removes code

A fold that adds net lines is a mistake.

## Do not

- Fix bugs found on the way. Note them. A silent behaviour fix breaks the lock, then hides in a diff full of moved lines.
- Invent an idiom the file does not use. Match brace style, naming, comment density. Read the directory's .md.
- Reach past the project's own wrappers to save a line.
- Touch anything outside the target.

## Report

aLoC before and after, raw line delta, and a table of folds with the occurrence count justifying each.