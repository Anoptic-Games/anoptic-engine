---
name: create-test
description: Create a test or benchmark in tests/ using the tests/templates primitives
argument-hint: "[module or behavior under test]"
---
Create `tests/anotest_<name>.c` for $ARGUMENTS. Register it in `tests/CMakeLists.txt`.

Conventions:
- Exit 0 = pass. On failure print what broke, return nonzero.
- Deterministic: fixed seeds, seconds of runtime. argv[1] scales iterations for a soak.
- Benchmarks always build (no rot), DISABLED in ctest, run by hand. They print a table and exit 0.
- File banner says what is covered. A concurrency test also states its oracle (eg: lines out == records in).
- Register: add_executable, link anoptic_core (anoptic_render for Vulkan), add_test, TIMEOUT + LABELS. Labels: unit, concurrency, mem, mesh, render, vulkan, fuzz, bench, optional.
- File-writing tests get ANO_TEST_OUTDIR="${CMAKE_CURRENT_BINARY_DIR}" and delete their scratch on exit.

Templates via `#include "templates/<x>.h"`. Header-only, no CMake changes:
- bench.h: bench_begin/bench_end tick a section. bench_lat records deltas into a caller buffer, overflow counts as lost. bench_lat_stats sorts once -> mean/min/p50/p90/p99/p99.9/max ns. bench_lat_header/row print the table. bench_ops_per_sec for throughput. Every latency claim reports percentiles. A mean cannot falsify a tail.
- rng.h: test_rng xorshift32, one per thread, fixed seed. rng_below, rng_printable, rng_fill_printable. Never rand() across threads.
- scratch.h: scratch_make_dir/remove_dir, the ANO_TEST_OUTDIR anchor, scratch_count_lines as the no-loss oracle.

Timed loops stamp raw ticks only. Convert and sort after the loop, in bench_lat_stats.

After: `build.bat 5` (or `./build.sh 5`), run the new binary by hand, show `git diff --stat`.
