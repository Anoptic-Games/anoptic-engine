---
name: bench-fps
description: Run the anopticengine FPS / GPU-pass benchmark and write the result to docs/benchmarks/. Use whenever asked to benchmark, run the sweep, measure frame throughput, or compare a shadow/render knob. Drives tools/perf/bench_fps_<platform>.py (win64 / linux / macos). NEVER invent a runner. Full methodology and every env knob live in tools/perf/bench_fps.md.
argument-hint: "[flags, e.g. --res 1920x1080, or default sweep]"
---

## Pick the driver

- Windows: `tools/perf/bench_fps_win64.py` (win32 + pywin32).
- Linux: `tools/perf/bench_fps_linux.py` (X11/Xwayland only, never native-Wayland GLFW; tools from Nix, never apt: `nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop`).
- macOS: `tools/perf/bench_fps_macos.py` (pyobjc via Nix, never brew: `nix-shell -p "python3.withPackages (ps: [ps.pyobjc-framework-Quartz ps.pyobjc-framework-Cocoa])" --run "python3 tools/perf/bench_fps_macos.py"`). Sizes/positions via engine launch knobs (`ANO_RES`/`ANO_POS`/`ANO_FLOAT`/`ANO_MENU`). `--res` is framebuffer pixels; driver converts to points by `backingScaleFactor`. `--churn` unsupported.
- Never hand-roll a launch/measure loop. Drivers own foreground-verify, physical-pixel sizing, and fresh-process-per-point.

## Prereqs

- A Release exe must exist: `build\Release\anopticengine.exe` (Windows) / `build/Release/anopticengine` (Linux, macOS). Build first if missing (`build.bat 1` / the Release preset / `./build.sh 1`).
- Confirm branch and commit up front. Silent `[frame]`/`[profile]` log usually means wrong branch or stale exe.

## Run

Default resolution sweep, HUD menu open, shadow-culled (`ANO_SHADOW_BUDGET=2`, the harness default):
```bash
python tools/perf/bench_fps_win64.py
```
Flags, same on all drivers:
- `--res WxH`: single resolution instead of the sweep.
- `--dur S`: seconds per point. Default 30 s. Too short for GPU profile windows at that fps -> nonzero exit. Never shorten below default.
- `--no-menu`: static HUD only.
- `--churn`: resize-storm stress, single row.
- `--env KEY=VAL`: engine env override. `--env ANO_SHADOW_BUDGET=0` for uncapped baseline.
- `--exe PATH`: non-default binary.

Standing env: every run passes `ANO_SHADOW_BUDGET=2`; mac always adds `MTL_HUD_ENABLED=1 MTL_HUD_VISIBLE=1`. Drivers set these by default. Extra config via `--env` only.

Sweep: standard ladder points the display can realize, topped by display-max (out-of-range dropped with a printed note). Full sweep = those points × `--dur` s. Long runs: background and tee to `./scratch`; Python block-buffers through a pipe, so rows land at the end:
```bash
python tools/perf/bench_fps_win64.py 2>&1 | tee ./scratch/sweep.log
```

## Read the output

- First line is `ENV_VARS: ...`. Keep verbatim for the writeup.
- Next lines: measured display (native panel, desktop mode, scale, largest realizable framebuffer) and any dropped ladder points. System/Mode facts for the writeup.
- One row per point. `front` must read `FRONT`. DROP any `BG!!` row.
- `render` is realized swapchain extent from `res=`. Compare rows by it; `target` is only the request. `swap` MiB ~linear with `render` pixels. `?` render means exe predates the `res=` profile line.
- `wall/cap >= 0.9` is GPU-bound; below is CPU/present-bound. `?` bound means run too short for GPU profile windows; driver exits nonzero on any `?` row (churn excepted). Rerun with longer `--dur`; never publish. `frusta` is shadow renders/frame, ~8 under budget-2, ~26 uncapped.
- Harness drops warmup and takes per-point medians. Do not re-average.

## Write it up

- EVERY run gets recorded: one `docs/benchmarks/YYYY-MM-DD-<label>.md` per run, verification and loaded-machine included. Note conditions in a line. Validity judgments belong to matei3d.
- Copy `docs/benchmarks/template.md` to `docs/benchmarks/YYYY-MM-DD-<label>.md`, labelling the config (e.g. `-bench`, `-bench2`, `-uncapped`).
- Fill System from the machine (spec-gathering commands in the template's HTML comment), Runtime environment from git + build, and the `ENV_VARS:` line from harness output verbatim.
- Paste table rows into Results, dropping `BG!!`. Resolution check: `swap` MiB per megapixel of `render` across the sweep, and top-to-1080p swap ratio vs pixel ratio. Top row is display max; label varies per machine.
- Comparing two configs: match `--dur` across both so rows line up.

## Hard rules

- Always use the platform driver. Never write an ad-hoc runner.
- Never trust or publish a `BG!!` row.
- `render` (engine `res=`) decides rendered resolution; `swap=` cross-checks; target label decides nothing.
- Do not `git commit` the results doc without approval.
- Read `tools/perf/bench_fps.md` before interpreting anything unusual. Engine-side log contract, full env-knob list, second-machine reproduction checklist.
