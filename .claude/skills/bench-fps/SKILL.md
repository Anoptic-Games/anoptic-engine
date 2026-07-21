---
name: bench-fps
description: Run the anopticengine FPS / GPU-pass benchmark and write the result to docs/benchmarks/. Use whenever asked to benchmark, run the sweep, measure frame throughput, or compare a shadow/render knob. Drives tools/perf/bench_fps_<platform>.py (win64 / linux / macos). NEVER invent a runner. Full methodology and every env knob live in tools/perf/bench_fps.md.
argument-hint: "[flags, e.g. --res 1920x1080, or default sweep]"
---

## Pick the driver

- Windows: `tools/perf/bench_fps_win64.py` (win32 + pywin32).
- Linux: `tools/perf/bench_fps_linux.py` (X11/Xwayland only, never native-Wayland GLFW; tools from Nix, never apt: `nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop`).
- macOS: `tools/perf/bench_fps_macos.py` (pyobjc via Nix, never brew: `nix-shell -p "python3.withPackages (ps: [ps.pyobjc-framework-Quartz ps.pyobjc-framework-Cocoa])" --run "python3 tools/perf/bench_fps_macos.py"`). Sizes/positions the window through the engine launch knobs (`ANO_RES`/`ANO_POS`/`ANO_FLOAT`/`ANO_MENU`) because external resize and key injection are Accessibility-gated. `--res` values are framebuffer pixels; the driver converts to points by `backingScaleFactor`. `--churn` unsupported.
- Never hand-roll a launch/measure loop. The drivers own foreground-verify, physical-pixel sizing, and fresh-process-per-point; a hand-rolled loop mismeasures the GPU passes.

## Prereqs

- A Release exe must exist: `build\Release\anopticengine.exe` (Windows) / `build/Release/anopticengine` (Linux, macOS). Build first if missing (`build.bat 1` / the Release preset / `./build.sh 1`).
- Confirm branch and commit up front. The release-visible `[frame]`/`[profile]` log lines only exist past the commit that added them, so a silent log usually means the wrong branch or a stale exe.

## Run

Default resolution sweep, HUD menu open, shadow-culled (`ANO_SHADOW_BUDGET=2`, the harness default):
```bash
python tools/perf/bench_fps_win64.py
```
Flags, same on all drivers:
- `--res WxH` 〜 single resolution instead of the sweep.
- `--dur S` 〜 seconds per point. Every run lasts exactly 30 s unless this overrides it. A run too short for GPU profile windows at its fps fails with a nonzero exit 〜 never shorten below the default to save time.
- `--no-menu` 〜 static HUD only.
- `--churn` 〜 resize-storm stress, single row.
- `--env KEY=VAL` 〜 repeatable engine env, overrides the defaults. Pass `--env ANO_SHADOW_BUDGET=0` for the uncapped baseline.
- `--exe PATH` 〜 non-default binary.

Standing env: every run passes `ANO_SHADOW_BUDGET=2`; mac runs additionally always pass `MTL_HUD_ENABLED=1 MTL_HUD_VISIBLE=1` (the Metal performance HUD is part of the standard mac config). The drivers set all of these by default 〜 pass extra config through `--env`, never by hand-rolling the environment.

The sweep derives from the measured display: standard ladder points the display can realize, topped by the display-max point (out-of-range points like 4K on a smaller panel are dropped, with a printed note). A full sweep is those points times `--dur` seconds. For long runs, background it and tee to a repo `./scratch` file; Python block-buffers through a pipe, so rows land at the end, not live:
```bash
python tools/perf/bench_fps_win64.py 2>&1 | tee ./scratch/sweep.log
```

## Read the output

- First line is `ENV_VARS: ...` 〜 the engine env the run used. Keep it verbatim for the writeup.
- The next lines report the measured display (native panel, desktop mode, scale, largest realizable framebuffer) and any ladder points dropped for exceeding it. Keep them for the writeup: they are the System/Mode facts.
- One row per point. The `front` column must read `FRONT`. DROP any `BG!!` row, a background window inflates the GPU passes.
- `render` is the realized swapchain extent, parsed from the engine's `res=` 〜 compare rows by it; `target` is only the request. `swap` MiB is the cross-check: ~linear with `render` pixels. A `?` render means the exe predates the `res=` profile line.
- `wall/cap >= 0.9` is GPU-bound, below is CPU/present-bound. A `?` bound means the run was too short for GPU profile windows at that fps; the driver exits nonzero on any `?` row (churn excepted) 〜 rerun with a longer `--dur`, never publish it. `frusta` is shadow renders/frame, ~8 under the budget-2 default, ~26 uncapped.
- The harness already drops warmup and takes per-point medians. Do not re-average.

## Write it up

- EVERY run gets recorded 〜 one separate `docs/benchmarks/YYYY-MM-DD-<label>.md` per run, verification runs and loaded-machine runs included. Note the conditions in a line; recording is unconditional, validity judgments belong to matei3d.
- Copy `docs/benchmarks/template.md` to `docs/benchmarks/YYYY-MM-DD-<label>.md`, labelling the config (e.g. `-bench`, `-bench2`, `-uncapped`).
- Fill System from the machine (spec-gathering commands are in the template's HTML comment), Runtime environment from git + build, and the `ENV_VARS:` line from the harness output verbatim.
- Paste the table rows into the Results table, dropping `BG!!` rows, then do the resolution check: `swap` MiB per megapixel of `render` pixels across the sweep, and the top-to-1080p swap ratio against the same rows' pixel ratio. The top row is the display max, so its label varies per machine.
- Comparing two configs (culled vs uncapped, MSAA levels, a feature toggle): match `--dur` across both so the rows line up.

## Hard rules

- Always use the platform driver. Never write an ad-hoc runner.
- Never trust or publish a `BG!!` row.
- `render` (the engine's `res=`) decides the rendered resolution; `swap=` cross-checks it; the target label decides nothing.
- Do not `git commit` the results doc without approval.
- Read `tools/perf/bench_fps.md` before interpreting anything unusual. It carries the engine-side log contract, the full env-knob list, and the second-machine reproduction checklist.
