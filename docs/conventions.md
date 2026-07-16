# Conventions of the Anoptic Engine

This file keeps track of the formally confirmed conventions, stylistic guidelines, et cetera we have decided on for this anoptic engine.


## Include Policy

Covering those matters regarding the C language's fabled `include` directive. Lacking the features of higher-level languages, such as namespaces, package management, crates, modules, and so on, we rely on discipline and clear guidelines to stay our hand and keep the modular architecture working smoothly.

### Interfaces

At some higher level, a large program that actually works can be split up into modules. Different parts that are, in theory, independent from one another except for where they interact. The Anoptic Engine uses the abstract concept of modules internally, as well as in its interactions with external libraries. This enables effective platformatization (currently supporting x64 Windows, x64 Linux, and aarch64 MacOS on Apple Silicon), the updating of library dependencies, the possibility of several people working on 'their' part of the engine asynchronously, and the upgrading of certain parts of a program without affecting everything else.

Yes, as you might be thinking, the boundaries of a module are entirely arbitrary. It's up to a library author to use their common sense and group things up in a way that makes everyone's lives easier.

A module itself, then, can be split into two components: its surface, and its internals. In software, we call the surface another piece of code interacts with an interface. The internals are called an implementation, which can vary depending on the situation, while the interface remains consistent because other programs rely on it to keep functioning themselves.

C gets a lot of crap for splitting its source code up into two file types: `.h` header files, and `.c` files. Notice that for Anoptic Engine, this distinction happens to coincide almost exactly with the surface / internals split modularization requires. 

> CONVENTION: It might be more helpful to think of `.h` as Interface Files, and of `.c` as Implementation Files.

Fun Fact for 2026: This modularization also makes it easier for Large Language Models to know what exactly they're working on, and to avoid overstepping into code you don't want them touching. It even helps you save on input tokens! 

What follows are instructions on how to use them.

### anoptic_[Module].h

Anoptic Engine defines the high-level bucketing of code into a module inside of include/. An anoptic_<modulename>.h file is treated as a Module by our build configurations, and can be included anywhere in the program.

Whenever one module within Anoptic Engine needs to use another, it always does so through these headers.

> CONVENTION: a function defined in an anoptic module interface always begins with `ano_` !

If their surface is in `include/`, then their internals are found in `src/` each with a subdirectory matching the module's name.

### Intramodular helper includes

A module might need other .h files *inside* of itself, that aren't called by other parts of the programs but are helpful for utility. You can do this. Every src/ subdirectory has its own CMakeLists.txt and can arrange its code however it likes.

### External libraries

External libraries, ie software we didn't write ourselves, follows the calling conventions of whoever wrote them. Read their docs. External libraries include the operating system's handles, standard library implementations, pthread, glibc, Vulkan and so on.

> CONVENTION: When an external library is used all over different modules, it might be a good idea to wrap it inside of an `include/anoptic_module.h` !
> CONVENTION: When a library is platform-dependant and broadly used inside of many modules, it should always be wrapped inside of an `include/anoptic_module.h` (SEE: anoptic_threads.h and its implementation).

## Debugging

Debugging is important, and for its purposes we include a DEBUG BUILD separately from a RELEASE BUILD.

Aside from toggling between debug-symbols and optimization, a DEBUG BUILD might include additional code that never gets compiled into RELEASE, wrapped inside of an ifdef, for instance more verbose error messages.

> CONVENTION: an interface function beginning with `ano_debug_` is only included in Debug builds, and expands to ` ((void)0)` otherwise!