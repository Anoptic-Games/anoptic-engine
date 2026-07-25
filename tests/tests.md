# Tests

The ``tests/`` directory contains the source (not the binaries) for the various tests written to validate the rest of our code in ``src/``.

The suite runs via CTest through the platform build scripts (`build.sh` / `build.bat`); run either with no arguments for the profile list. See the Tests section of the root README for the current test list; `tests/CMakeLists.txt` is the source of truth, including which targets are built but disabled by default (`anotest_logbench`, `anotest_chariots`).

Tests check that a module and its parts BEHAVE correctly. They should not attempt to lock implementations in stone.

Tests need to be made for every possible usage of something in includes/, testing each modules boundaries and edge cases. In some instances, fuzzers should also be written and used, such as logfuzz.

Occasionally, we may also write synthetic benchmarks. These are always optional, see strbench and logbench.

## Suite membership

One behavioral suite per ``include/`` header, driven only through its public ``ano_*()`` surface, plus fuzzers where untrusted input enters, optional benches, and the ``anotest_vk_*`` intra-modular tier. No per-bug pin tests: a boundary case worth keeping lives as a case inside its module's suite. Implementation details have no contract and therefore nothing to test.
