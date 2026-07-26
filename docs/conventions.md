# Conventions of the Anoptic Engine

Formally confirmed conventions and stylistic guidelines for Anoptic Engine.


## Include Policy

C has no namespaces or packages. Module discipline keeps the architecture working.

### Interfaces

A module has a surface (interface) and internals (implementation). Anoptic uses modules for platformatization (x64 Windows, x64 Linux, aarch64 MacOS on Apple Silicon), dependency updates, and parallel work on separate parts.

Module boundaries are arbitrary. Group by common sense.

C's `.h` / `.c` split matches that surface / internals split.

> CONVENTION: It might be more helpful to think of `.h` as Interface Files, and of `.c` as Implementation Files.

Fun Fact for 2026: This modularization also makes it easier for Large Language Models to know what exactly they're working on, and to avoid overstepping into code you don't want them touching. It even helps you save on input tokens!

### anoptic_[Module].h

Module interfaces live in `include/anoptic_<modulename>.h`. Includeable anywhere. Cross-module use always goes through these headers. Implementations live in `src/<modulename>/`.

> CONVENTION: a function defined in an anoptic module interface always begins with `ano_` !

### Intramodular helper includes

A module may keep private `.h` helpers inside `src/<module>/` that other modules never include. Each `src/` subdirectory has its own CMakeLists.txt.

### External libraries

External libraries follow their own calling conventions. Read their docs. Includes OS handles, stdlib, pthread, glibc, Vulkan, etc.

> CONVENTION: When an external library is used all over different modules, it might be a good idea to wrap it inside of an `include/anoptic_module.h` !
> CONVENTION: When a library is platform-dependant and broadly used inside of many modules, it should always be wrapped inside of an `include/anoptic_module.h` (SEE: anoptic_threads.h and its implementation).

## Debugging

DEBUG BUILD separate from RELEASE BUILD. Debug may add extra code (e.g. verbose errors) behind ifdef; never compiled into Release.

> CONVENTION: an interface function beginning with `ano_debug_` is only included in Debug builds, and expands to ` ((void)0)` otherwise!
