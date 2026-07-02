# Tests

The ``tests/`` directory contains the source (not the binaries) for the various tests written to validate the rest of our code in ``src/``.

The suite runs via CTest through the platform build scripts (`build.sh` / `build.bat`); run either with no arguments for the profile list. See the Tests section of the root README for the current test list; `tests/CMakeLists.txt` is the source of truth, including which targets are built but disabled by default (`anotest_logbench`, `anotest_chariots`).