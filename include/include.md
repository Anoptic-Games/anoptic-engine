# ``include`` Directory

Public headers for the Anoptic Game Engine. Public API means FUNCTION SIGNATURES and the TYPE DEFINITIONS those signatures depend on. Never put platform-specific implementation details in `include/anoptic_xxx.h`.

IMPLEMENTATIONS of those signatures and definitions live in `src/{module}/`. Example: `include/anoptic_time.h` → `src/time/`.

Runtime C ABI functions in an `include/` header always begin with `ano_`; C++26 compile-time facilities live in namespace `ano`. This distinguishes both from internal and imported functions:
1. Public interface: surface of an engine module.
2. Written and sanctioned by us; anoptische (adjective form of anoptic).

## Directory Structure

```plaintext
include/
├── anogltf.h           # C++26 reflected glTF loader and typed schema
├── anoptic_meta.h      # Header-only C++26 reflection and value contracts
├── anoptic_memory.h    # Public memory allocation API
├── anoptic_memory_typed.h # C++ typed allocation extension
├── anoptic_threads.h   # Platform abstraction of pthread API
├── anoptic_threads_typed.h # C++26 typed lock-free transport
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
