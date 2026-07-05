---
name: tersify
description: Simplify comments and tighten naming in the given files
argument-hint: "[files or module]"
---
Simplify the FUCK out of the comments in $ARGUMENTS.
Terisfy should always preserve semantic meaning.

Rationale comments die:
- Comments say WHAT, never WHY. One line. eg: "# Sanitizer flags".
- Preserving meaning means the what. The why is disposable.
- Delete justification, design history, and alternatives-considered. "because", "so that", "previously", "not X because Y" are kill signals.
- A policy paragraph -> its one-line conclusion. eg: 13 lines of build philosophy -> "# Build policy: whole builds, ThinLTO Release, modern linkers, static linking."
- Decorated banners (# ---- Section ----) -> one plain header line.
- Inline comments are a bare noun. eg: `shaderc # glslc`
- Usage hints survive because they are a what. eg: "# Override with: set MSYS2_CLANG=..."
- Build files count: CMakeLists.txt, flake.nix, build.sh, build.bat.

Comments:
- One clause each. Cut dragging run-on sentences down to their meaning.
- No comment should have a ; or - or -- or — inside of it to split a sentence. Needing one means the sentence is too long, so truncate it and get the meaning across already. a -> b is ok.
- A comment needing paragraphs means the code is unclear. Rename a local instead.
- Use the shorter synonym when a dev reads it identically. eg: "longest formatted line" --> "max formatted line"
- Never allow comments to split mid-sentence. Make a newline or use /* */ for paragraphs sparingly.
- Keep my `/* Section */` comments and my own comments verbatim.
- Preserve semantic meaning.

Naming, in `src/` files:
- Local variables are camelCase, eg: g_buf_len --> g_bufLen
- Rename only where comments were doing a good name's work. Conservative, no behavior change. eg: p, m --> head, body

After: `./build.sh 3`, run tests, show `git diff --stat`.
