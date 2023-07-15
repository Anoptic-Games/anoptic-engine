# Build Directory

The `build/` directory is the designated location for the output of the compilation process, regardless of the build system utilized.

The target for the compiled binary varies based on its intended usage. The directory is categorized into three subdirectories: `debug`, `release`, and `test`.


### Debug

The `debug` subdirectory houses the debug builds of the software. These versions include modifications for development purposes, 
such as an adjusted memory allocator with explicit initialization, comprehensive logging, and additional development tools.


### Release

The `release` subdirectory contains optimized builds intended for public release. These versions reduce the logging verbosity,
may disable certain development features, and fine-tune all aspects for an optimal end-user experience.


### Test

The `test` subdirectory is for the compiled tests that correspond to individual submodules of the engine, as well as compiled integration tests.
