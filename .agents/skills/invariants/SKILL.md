---
name: invariants
description: Place a verified correctness obligation at its strongest enforceable layer. Use when fixing bugs, reviewing guards, or designing safe Anoptic C+Ultra interfaces.
---

# Placement order

Apply each useful layer, starting with the strongest:

1. Structure: make the path unreachable.
2. Types: make the state unrepresentable with domain types, enums, or constrained wrappers.
3. Compile time: use signatures, concepts, `constexpr`, `consteval`, reflection, or `static_assert`.
4. Runtime state: establish and preserve an explicit invariant.
5. Guard: reject what cannot be excluded earlier.

Prefer compile-time impossibility, then add lower layers only for independent failure sources. Keep boundary checks for untrusted or foreign input. Remove a guard only when an enforced upstream invariant excludes the case and failure cannot cause UB, corruption, or out-of-bounds access. Do not replace signed values with unsigned unless the arithmetic domain is genuinely modular or underflow-safe.

Implement and test the fix; report residual risks rather than producing a report-only workup.
