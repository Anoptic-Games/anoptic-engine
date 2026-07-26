# ``include`` Directory

Public headers for the Anoptic Game Engine. Public API means FUNCTION SIGNATURES and the TYPE DEFINITIONS those signatures depend on. Never put platform-specific implementation details in `include/anoptic_xxx.h`.

IMPLEMENTATIONS of those signatures and definitions live in `src/{module}/`. Example: `include/anoptic_time.h` → `src/time/`.

Functions in an `include/` header always begin with `ano_`, distinguishing them from internal and imported functions:
1. Public interface: surface of an engine module.
2. Written and sanctioned by us; anoptische (adjective form of anoptic).

## Directory Structure

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
