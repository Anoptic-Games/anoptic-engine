---
name: tersify
description: Simplify comments and local names in requested files without changing behavior or deleting operational knowledge.
---

# Pass

- Let code state the obvious what. Keep non-obvious why: invariants, safety constraints, ABI requirements, platform workarounds, measured performance facts, and usage instructions.
- Delete narration, restatement, superseded history, rejected alternatives, and rationale that no longer constrains the code.
- Compress policy prose to one enforceable conclusion. Prefer one idea per comment and short, complete sentences.
- Rename a local only when the better name removes a comment; remain conservative and preserve project naming style.
- In `src/`, keep local names camelCase.
- Preserve user-authored section banners unless asked otherwise.
- Touch only the requested scope. Do not fold a bug fix or behavioral refactor into the pass.

Run the existing relevant tests and show `git diff --stat` plus any rationale intentionally retained.
