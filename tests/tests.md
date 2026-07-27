# Tests

``tests/`` holds test sources (not binaries) validating ``src/``.

Suite runs via CTest through the platform build scripts (`build.sh` / `build.bat`); run either with no arguments for the profile list. See the Tests section of the root README for the current test list; `tests/CMakeLists.txt` is the source of truth, including which targets are built but disabled by default (`anotest_logbench`, `anotest_chariots`).

Tests check that a module and its parts BEHAVE correctly. They should not attempt to lock implementations in stone.

Cover every public usage in includes/, each module's boundaries and edge cases. Fuzzers where warranted, e.g. logfuzz.

Optional synthetic benchmarks: strbench, logbench.

## Suite membership

One behavioral suite per ``include/`` header, driven only through its public ``ano_*()`` surface, plus fuzzers where untrusted input enters, optional benches, and the ``anotest_vk_*`` intra-modular tier. No per-bug pin tests: a boundary case worth keeping lives as a case inside its module's suite. Implementation details have no contract: nothing to test.
