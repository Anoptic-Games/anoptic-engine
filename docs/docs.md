# Documentation

## Module structure

Every engine subsystem is a module: a public interface in `include/` paired with an implementation folder in `src/`.

- **`include/anoptic_<module_name>.h` — the contract.** Public surface. Constants, type aliases, function signatures. All a caller ever sees. Functions surfaced by `anoptic_<module_name>.h` always begin with `ano_` and are always lowercase.
- **`src/<module_name>/` — the secret.** The implementation. It can contain private
  headers (`<module_name>_<platform>.h`) included only by files *inside* the module.
  Implementation details, raw `pthread_*`/Win32 prototypes, internal structs,
  helper declarations all live here and never leak into `include/`.
- **Platform splits live in `src/`.** A common `<module_name>.c` always compiles.
`<module_name>_<platform>.c`, eg `timers_linux.c`, `timers_win64.c`, are platform-specific, dynamically selected by the CMakeLists.txt.

### Example

The module's `CMakeLists.txt` selects exactly one platform implementation:

```cmake
# src/<mod>/CMakeLists.txt

target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/<mod>.c)

if (WIN32)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/<mod>_win64.c)
elseif (APPLE)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/<mod>_macos.c)
elseif (UNIX)
    target_sources(anoptic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/<mod>_linux.c)
endif()
```

A private header carries implementation detail:
```c
// src/threads/threads_macos.h  — included ONLY by threads_macos.c
// macOS libpthread declares no spinlock/barrier primitives; we supply them.
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_barrier_wait(pthread_barrier_t *barrier);
// ...
```

### Idiomatic vs not

❌ **Not anoptic** — an implementation bleeding into the public header

✅ **Anoptic** — the header signature stays `ano_xxxxx()`