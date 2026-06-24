---
name: tersify
description: Simplify comments and tighten naming in the given files
argument-hint: "[files or module]"
---
Simplify the FUCK out of the comments in $ARGUMENTS.

Comments:
- One clause each. No run-ons. No `;` to split a sentence — use an em dash.
- A comment needing paragraphs means the code is unclear. Rename a local instead.
- Use the shorter synonym when a dev reads it identically. eg: "longest formatted line" --> "max formatted line"
- Keep my `/* Section */` comments and my own comments verbatim.

Naming, in `src/` files:
- Local variables are camelCase, eg: g_buf_len --> g_bufLen
- Rename only where comments were doing a good name's work. Conservative, no behavior change. eg: p, m --> head, body

After: `./build.sh 3`, run tests, show `git diff --stat`.
