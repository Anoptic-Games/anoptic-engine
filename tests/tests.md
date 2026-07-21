# Tests

The ``tests/`` directory contains the source (not the binaries) for the various tests written to validate the rest of our code in ``src/``.

The suite runs via CTest through the platform build scripts (`build.sh` / `build.bat`); run either with no arguments for the profile list. See the Tests section of the root README for the current test list; `tests/CMakeLists.txt` is the source of truth, including which targets are built but disabled by default (`anotest_logbench`, `anotest_chariots`).

## Suite membership (note)

The bughunt leaves a large `*guard` pin set beside the module/scene suite. That is intentional for the census phase; it is not a reason to delete pins once swoops turn them green.

Worst-case shape if the tree starts to feel noisy:

- **CORE** 〜 module/scene/contract tests that prove the engine still works.
- **EVERYTHING** 〜 CORE + every `*guard` + fuzz + optional harnesses.
- **retired/** 〜 guards whose root-cause swoop landed and whose pin is redundant with a CORE property test; keep the source, drop from CORE (and optionally from default EVERYTHING).

Prefer folders + a CMake option / ctest label (`ANO_TEST_SUITE=core|all`) over a git submodule until tests need their own repo cadence. A submodule only pays off if the census grows into a second remote or you ship trees without it.
