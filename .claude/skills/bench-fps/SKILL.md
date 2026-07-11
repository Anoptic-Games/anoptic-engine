---
name: bench-fps
description: Run the anopticengine FPS / GPU-pass benchmark and write the result to docs/benchmarks/. Use whenever asked to benchmark, run the sweep, measure frame throughput, or compare a shadow/render knob. Drives tools/perf/bench_fps_<platform>.py (win64 / linux). NEVER invent a runner. Full methodology and every env knob live in tools/perf/bench_fps.md.
argument-hint: "[flags, e.g. --res 1920x1080, or default sweep]"
---

## Pick the driver

- Windows: `tools/perf/bench_fps_win64.py` (win32 + pywin32).
- Linux: `tools/perf/bench_fps_linux.py` (X11/Xwayland only, never native-Wayland GLFW; tools from Nix, never apt: `nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop`).
- macOS: no driver yet (TODO `bench_fps_macos.py`). Do not improvise one.
- Never hand-roll a launch/measure loop. The drivers own foreground-verify, physical-pixel sizing, and fresh-process-per-point; a hand-rolled loop mismeasures the GPU passes.

## Prereqs

- A Release exe must exist: `build\Release\anopticengine.exe` (Windows) / `build/Release/anopticengine` (Linux). Build first if missing (`build.bat 1` / the Release preset).
- Confirm branch and commit up front. The release-visible `[frame]`/`[profile]` log lines only exist past the commit that added them, so a silent log usually means the wrong branch or a stale exe.

## Run

Default resolution sweep, HUD menu open, shadow-culled (`ANO_SHADOW_BUDGET=2`, the harness default):
```bash
python tools/perf/bench_fps_win64.py
```
Flags, same on both drivers:
- `--res WxH` — single resolution instead of the sweep.
- `--dur S` — seconds per point (default 45).
- `--no-menu` — static HUD only.
- `--churn` — resize-storm stress, single row.
- `--env KEY=VAL` — repeatable engine env, overrides the `ANO_SHADOW_BUDGET=2` default. Pass `--env ANO_SHADOW_BUDGET=0` for the uncapped baseline.
- `--exe PATH` — non-default binary.

A full sweep is 6 points times `--dur` seconds. For long runs, background it and tee to a repo `./scratch` file; Python block-buffers through a pipe, so rows land at the end, not live:
```bash
python tools/perf/bench_fps_win64.py --dur 15 2>&1 | tee ./scratch/sweep.log
```

## Read the output

- First line is `ENV_VARS: ...` — the engine env the run used. Keep it verbatim for the writeup.
- One row per point. The `front` column must read `FRONT`. DROP any `BG!!` row, a background window inflates the GPU passes.
- `swap` MiB is the resolution authority, not the window label. It scales ~linearly with pixels, so a 4K row reads ~4x its 1080p swap. A mismatch means a DPI-mislabel.
- `wall/cap >= 0.9` is GPU-bound, below is CPU/present-bound. `frusta` is shadow renders/frame, ~8 under the budget-2 default, ~26 uncapped.
- The harness already drops warmup and takes per-point medians. Do not re-average.

## Write it up

- Copy `docs/benchmarks/template.md` to `docs/benchmarks/YYYY-MM-DD-<label>.md`, labelling the config (e.g. `-bench`, `-bench2`, `-uncapped`).
- Fill System from the machine (spec-gathering commands are in the template's HTML comment), Runtime environment from git + build, and the `ENV_VARS:` line from the harness output verbatim.
- Paste the table rows into the Results table, dropping `BG!!` rows, then do the resolution check: `swap` MiB per megapixel across the sweep, and the 4K-to-1080p swap ratio against the 4x pixel ratio.
- Comparing two configs (culled vs uncapped, MSAA levels, a feature toggle): match `--dur` across both so the rows line up.

## Hard rules

- Always use the platform driver. Never write an ad-hoc runner.
- Never trust or publish a `BG!!` row.
- `swap=`, never the window label, decides the rendered resolution.
- Do not `git commit` the results doc without approval.
- Read `tools/perf/bench_fps.md` before interpreting anything unusual. It carries the engine-side log contract, the full env-knob list, and the second-machine reproduction checklist.
