# Resource manager verification matrix

Freeze item 15. The cell skeleton for the whole Phase A migration (M0..M19): every row is a platform-and-profile the resource manager claims to work on, and a cell is filled ONLY by someone who actually ran it and kept the log. An UNRUN cell is a fact about our evidence, not a defect to be tidied away, and it stays UNRUN until a real run replaces it.

Rules, so this file cannot rot into decoration:

- A cell is RUN only if the raw log exists at the recorded path. No log, no cell.
- The commit sha is the sha the binaries were built from. If the working tree was dirty, that is recorded too: a dirty run is evidence about a working tree, not about a commit.
- macOS is UNRUN. It is never claimed green. The repro commands are written down so that whoever gets an arm64 Mac can fill the row without asking anyone what to type.
- A profile that refuses to run on a platform (Windows sanitizers) is recorded as N/A with the reason, not as a pass and not as a failure.
- Result records the enabled/total tally that ctest actually printed, not a rounded "all green".

## Baseline — M0

Commit: `248002030086d872c500a66b509dab5932c988ae` (branch `feature-resourcemgr`).

**Working tree DIRTY at the time of these runs** (40 paths per `git status --porcelain`): the in-flight resource-manager work already present on the branch, plus M0's own harness fixes to `anotest_resgfx.c`, `anotest_resources.c`, `anotest_resfault.c` and `anotest_resbench.c`. The two Windows cells below therefore certify *that tree*, not a clean checkout of that sha. Said out loud because a matrix that quietly implies otherwise is worse than no matrix.

Windows toolchain: MSYS2 clang64, clang 22.1.8, cmake 3.31.6, Ninja, Windows 11 Pro 26200.

| Row | Commit sha | Platform | Profile | Result | Raw log | Date |
|---|---|---|---|---|---|---|
| Windows `build.bat 5` | 2480020 (dirty) | Windows 11 x64, MSYS2 clang64 | Debug -O0 + ctest | **PASS — 30/30 enabled** (41 registered, 11 DISABLED) | `docs/resourcemgr/logs/m0-win-debug-ctest.log` | 2026-07-13 |
| Windows `build.bat 8` | 2480020 (dirty) | Windows 11 x64, MSYS2 clang64 | Release -O3 + ctest | **PASS — 28/28 enabled** (41 registered, 13 DISABLED) | `docs/resourcemgr/logs/m0-win-o3-ctest.log` | 2026-07-13 |
| Windows `build.bat 6/7` | — | Windows 11 x64 | ASan/UBSan, TSan | **N/A** — `build.bat` exits 1 by design: MinGW clang supports neither TSan nor a working ASan against ucrt. Covered by WSL 6/7. | — | — |
| WSL `build.sh 5` | — | WSL2 Debian x64 | Debug -O0 + ctest | **UNRUN** | — | — |
| WSL `build.sh 6` | — | WSL2 Debian x64 | ASan + UBSan | **UNRUN** | — | — |
| WSL `build.sh 7` | — | WSL2 Debian x64 | TSan | **UNRUN** | — | — |
| WSL `build.sh 8` | — | WSL2 Debian x64 | Release -O3 + ctest | **UNRUN** | — | — |
| 9P remote-FS floor | — | WSL2 Debian, tree on the 9P `/mnt/c` mount | Debug + resource-labelled ctest | **UNRUN** | — | — |
| Engine smoke | — | Windows 11 x64 + Vulkan | Release engine, launch + render + clean exit | **UNRUN** | — | — |
| macOS | — | macOS arm64 (Apple Silicon) | any | **UNRUN — never claimed green.** No arm64 Mac on this host. | — | — |

The 28-vs-30 difference in the two Windows cells is not a regression: the O3 profile additionally disables `anotest_vk_compliance_layers` and `anotest_vk_sync`. Both Windows rows count only tests ctest actually enabled; the 11 (resp. 13) DISABLED targets are benches and the easter egg, and `anotest_resbench` is one of them.

The WSL rows are UNRUN, not unrunnable: this host has WSL2 Debian installed. They were simply not run in the M0 session, and saying "not run" is the entire point of the column.

## Freeze frontier — M1

Commit: `248002030086d872c500a66b509dab5932c988ae`, same sha as M0, tree still DIRTY and now carrying the whole Phase A campaign: the two M0-era red tests fixed (orphan-temp save recovery implemented, keybindings padding zeroed at the source), the W0 freeze seams (`resources_place.h` and the M2–M17 skeleton TUs), and the 10 new contract test binaries. These cells certify that tree. That dirty tree was subsequently committed as the Stage A checkpoint commit — the commit that introduces this section — with no source changes after any run below (docs only), so the cells reproduce from that commit.

Windows toolchain as M0. WSL cells: WSL2 Debian (kernel 6.18.33.2-microsoft-standard-WSL2), nix-provided clang (toolchain `debug_clang-linux-x64.cmake`), tree built in place on the 9P `/mnt/c` mount per the raw log's dirty-tree warning and the in-repo `build/Tests-TSan` artifacts.

| Row | Commit sha | Platform | Profile | Result | Raw log | Date |
|---|---|---|---|---|---|---|
| Windows `build.bat 5` | 2480020 (dirty) | Windows 11 x64, MSYS2 clang64 | Debug -O0 + ctest | **PASS — 40/40 enabled** (51 registered, 11 DISABLED) | `docs/resourcemgr/logs/m1-win-debug-ctest.log` | 2026-07-13 |
| Windows `build.bat 8` | 2480020 (dirty) | Windows 11 x64, MSYS2 clang64 | Release -O3 + ctest | **PASS — 38/38 enabled** (51 registered, 13 DISABLED) | `docs/resourcemgr/logs/m1-win-o3-ctest.log` | 2026-07-13 |
| WSL `build.sh 7` | 2480020 (dirty) | WSL2 Debian x64, nix clang, tree on 9P `/mnt/c` | TSan + ctest | **PASS — 33/33 enabled** (45 registered, 12 DISABLED incl. blackbox), **zero ThreadSanitizer reports**; `anotest_resownership` ran live (2.26 s) | `docs/resourcemgr/logs/m1-wsl-tsan-ctest.log` | 2026-07-13 |
| WSL `build.sh 6` | 2480020 (dirty) | WSL2 Debian x64, nix clang, tree on 9P `/mnt/c` | ASan + UBSan + ctest | **35/39 enabled, 4 failed** — all 4 failures are `anotest_vk_*` dying at `FATAL Failed to initialize GLFW!` (headless WSL, no display/Vulkan ICD): an environment artifact, not a code failure, recorded as ctest printed it. All 18 resource-family tests passed. **Zero ASan/UBSan/LeakSanitizer reports.** | `docs/resourcemgr/logs/m1-wsl-asan-ctest.log` | 2026-07-13 |
| WSL `build.sh 5` | 2480020 (dirty) | WSL2 Debian x64, nix clang, tree on 9P `/mnt/c` | Debug -O0 + ctest | **35/40 enabled, 5 failed** — the same 4 headless `anotest_vk_*` GLFW failures, plus `anoptic_blackbox` failing at 94.37 s with no captured output. The blackbox failure is now attributed (rerun with output, `docs/resourcemgr/logs/m1-wsl-blackbox-attribution.log`): `ring_full: wrong death: signal 14` — the scenario is a producer storm that must end in a deliberate SEGV (`anotest_blackbox.c:385`), but the logger's 5 s deadman (SIGALRM) fired first because the drainer flushing to 9P `/mnt/c` cannot keep up. This is the journal's documented 9P deadman starvation, not a code failure; the same test passes in 8.97 s on native NTFS in the Windows Debug cell above. All 18 resource-family tests passed. | `docs/resourcemgr/logs/m1-wsl-debug-ctest.log` | 2026-07-13 |
| 9P remote-FS floor | — | WSL2 Debian, tree on the 9P `/mnt/c` mount | Debug + resource-labelled ctest | **UNRUN** as specified (the TSan cell above did build and test on 9P, which is supporting evidence but not this cell's `build.sh 5` + `-L resource` recipe) | — | — |
| Engine smoke | 2480020 (dirty) | Windows 11 x64, RTX 4090, Vulkan | Release engine, 18 s foreground run | **PASS** — three memory-face fonts served by the manager, scene renders at ~660–690 fps @ 800x600 shadow profile, zero error/fatal lines. Run was hard-killed: the clean-exit autosave-commit path was NOT exercised. | `docs/resourcemgr/logs/m1-win-smoke-engine.log` | 2026-07-13 |
| macOS | — | macOS arm64 (Apple Silicon) | any | **UNRUN — never claimed green.** | — | — |

The TSan cell is the load-bearing new fact: the comprehensive-report (§6) listed TSan cleanliness of the new lock-free publication scheme as claimed-only on this tree, and this run discharges that with a real log. The `anotest_resownership` oracle remains weak (99.7% of reader observations are stale sentinels — measured this session: complete=5658, stale=1717390 over 1000 iterations), so "TSan-clean" is evidence about the current interleavings, not a proof of the protocol; the blueprint's oracle-strengthening item stands.

## Repro commands

Every command is run from the repo root. Only one build at a time: the build directory is shared.

Windows (MSYS2 clang64 on PATH):

```
build.bat 5        # Debug -O0, builds + runs ctest        -> build/Tests
build.bat 8        # Release -O3, builds + runs ctest      -> build/O3Tests
```

WSL2 Debian / Linux (from `nix develop`, or with clang + cmake + ninja on PATH):

```
./build.sh 5       # Debug -O0 + ctest
./build.sh 6       # + AddressSanitizer/UBSan
./build.sh 7       # + ThreadSanitizer
./build.sh 8       # Release -O3 + ctest
```

9P remote-FS floor. The point of this cell is precisely the environment the Linux perf numbers are taken *off* of: a filesystem that short-reads, lies about size and mtime, and delays visibility. So it is run with the tree left ON the 9P mount (`/mnt/c/...`), never on a copy in ext4:

```
wsl -d Debian
cd /mnt/c/Users/Pyrus/Code/anoptic-engine     # the 9P mount, deliberately not copied to ext4
./build.sh 5
ctest --test-dir build/Tests -L resource --output-on-failure
```

Expect it to be slow (9P distorts every IO-bound number, and per `RESOURCE_MANAGER_IMPL.md` it starves the blackbox deadman). This cell certifies CORRECTNESS on a hostile FS. No performance figure may be quoted from it.

Engine smoke:

```
build.bat 1                 # Release engine (needs the Vulkan SDK)
build/Release/anopticengine.exe
```

macOS arm64 — UNRUN. To fill the row, on an Apple Silicon Mac:

```
nix develop                 # flake.nix provides the whole toolchain
./build.sh 5                # Debug + ctest
./build.sh 6                # ASan/UBSan
./build.sh 7                # TSan
./build.sh 1                # Release engine
```

Then screenshot the running app with `.claude/tools/screenshot-macos` (never osascript). Until someone does this on real hardware, macOS stays UNRUN in this table. It does not get a green cell for compiling.

## M0 harness defects fixed

Recorded here because these three bugs silently corrupted the evidence this matrix is made of, and every later row depends on them staying fixed.

1. `tests/anotest_resgfx.c` — `if (failures) return;` sat between the scene's count checks and every per-field ground-truth assertion, keyed on the GLOBAL failure counter. Once any earlier check anywhere in the run had failed, every vertex, index, node, transform and material-factor assertion was skipped in silence, and the test still reported the original failure only. A staging typo could hide an entire wrong scene. The gate is now scoped to the four count checks and to this scene's own array pointers, and an unusable shape is a LOUD failure instead of a quiet skip.

2. `tests/anotest_resources.c` and `tests/anotest_resfault.c` — the no-temp-litter oracle lived inside `#ifndef _WIN32` / `<dirent.h>`, so it was compiled out on the one platform whose `ReplaceFileW` path is likeliest to strand a temp. Both now use `rmos_scan_dir`, which is real on both platforms.

   This immediately caught something, and it is worth writing down: on Windows the fault harness `longjmp`s out of `ano_res_write` with the temp file's HANDLE still open, and `rmos_open_excl` takes share mode 0 — so those temps cannot be deleted in-process (POSIX `unlink` does not care, which is exactly why the dirent-only oracle never saw it). Two temps per run, from `RES_FAULT_AFTER_WRITE` and `RES_FAULT_AFTER_SYNC`. That is a property of the HARNESS, not of the write protocol: a real crash closes the handle and leaves a deletable orphan for the recovery path. So the oracle is now a DELTA — the clean write must strand no NEW temp — which still fails loudly if `ReplaceFileW` ever leaves its own temp behind, and no longer lies about which platform it ran on.

3. `tests/anotest_resbench.c` — the `res_registry_stats()` shim set `direct_bytes = pools.live_bytes = ano_res_stats().live_bytes`, i.e. it aliased ONE field to two names, then compared them, printed their difference as a "direct" figure, and SUMMED them into a "residual footprint". The manager exposes no direct-vs-pooled split today, so every number it produced was a tautology wearing a measurement's name. The shim is deleted; the file reads `ano_res_stats()` directly and reports live and chunk deltas side by side, never summed (they overlap — pooled live bytes are served out of chunks).

   **No figure from that shim, or from anything in `anotest_resbench.c`, may enter a contest table.** The real allocator-hierarchy numbers arrive with the telemetry cells (M3) and the placement seam (M5/M6), and land in `docs/benchmarks/` per §7.5 of the blueprint.
