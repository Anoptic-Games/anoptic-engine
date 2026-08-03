# Anoptic Engine conventions

## C+Ultra

In-tree `.c` files compile as C++26 while retaining C-shaped source, data layout, and filenames. Use lambdas, namespaces, strong types, concepts, templates, `constexpr`, `consteval`, and reflection where they remove runtime work or duplicated declarations. Prefer plain data and value semantics. Do not introduce inheritance, virtual polymorphism, RTTI, exceptions, or unnecessary ownership frameworks. Preserve measured hot-path behavior and the `-nostdlib++` build.

## Modules

- Public interfaces live in `include/anoptic_<module>.h`; implementations live in `src/<module>/`.
- Cross-module calls use public headers and C ABI surfaces where practical. Public engine functions begin with `ano_`.
- Private helpers stay inside `src/<module>/`; use namespaces for private C++ names.
- Each source module owns its `CMakeLists.txt`.

Wrap a foreign library behind an Anoptic interface when it is broadly used or platform-dependent. Otherwise follow that library's native calling convention.

## Safety and builds

Express invariants structurally, in types, or at compile time before adding runtime guards. Keep checks at untrusted and foreign boundaries.

For renderer verification from WSL, use `nix build .#release-wsl`, stage the emitted `result/bin/anopticengine.exe` and runtime tree onto a Windows-local path, then launch the `.exe` from Windows. A missing in-guest Vulkan target is not a renderer build failure; do not invent an in-WSL Linux renderer workaround or rebuild the cross-toolchain by hand.

Debug-only diagnostics must not enter Release. Public `ano_debug_*` operations compile to `((void)0)` outside Debug.
