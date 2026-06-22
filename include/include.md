# ``include`` Directory

The `include/` directory contains the public header files for the Anoptic Game Engine. These header files define the public API that games use to interact with the engine. Public API, in Anoptic Engine, means that these are meant to be FUNCTION SIGNATURES and the TYPE DEFINITIONS those function signatures depend on. Platform-specific implementation details should never be in an include/anoptic_xxx.h file, as doing so may break delicate interactions between modules.  

The IMPLEMENTATIONS of these Signatures and Definitions, that is to say, the specific platform-specific instructions, particular memory allocation strategies, particular data structures employed in intermediary steps, etc, are all in `src/{subdirectory-matching-the-module-name}/`. For instance: `include/anoptic_time.h` has its platform-specific implementations in `src/time/`, and serves as a good example of how platform differences are handled.

Functions in an `include/`  header always begin with the prefix `ano_` to distinguish them from functions internal to a source file, as well as from any imported libraries. An `ano_` prefix makes it immediately evident that:
1.  It is probably a public interface function, that is to say the surface of an engine module.
2.  It is written by and sanctioned by us, it is anoptische (the adjective form of anoptic).

## Directory Structure

Here are some canonical examples of how these work.
```plaintext
include/
├── anoptic_memory.h    # Public memory allocation API
├── anoptic_threads.h   # Platform abstraction of pthread API
├── ...                 # other APIs
└── anoptic_time.h      # Public timekeeping API
```
```plaintext
src/
├── memory/             # Memory allocation implemented
├── threads/            # Platform abstraction of pthread implemented
├── ...                 # other implementations
└── time/               # Public timekeeping implemented
```