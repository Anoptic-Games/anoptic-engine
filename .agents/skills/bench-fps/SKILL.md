---
name: bench-fps
description: Run Anoptic's complete cross-resolution FPS/GPU-pass sweep and record its benchmark report. Use only for an explicitly requested full sweep, publication baseline, or resolution-dependence study. Never use for a quick single-resolution A/B.
---

# Full sweep

1. Confirm branch, commit, and Release executable; build if missing.
2. Read `tools/perf/bench_fps.md` for platform prerequisites and interpretation, then run `tools/perf/bench_fps_<platform>.py` without `--res`. The driver derives the complete realizable resolution ladder. Never hand-roll launch, focus, resizing, or measurement.
3. Apply the user-supplied report label and flags only as sweep configuration. Keep the 30-second default unless more GPU profile windows are required. For differential sweeps, run reference and candidate with identical arguments and environment.
4. Accept only `FRONT` rows. Compare realized `render`, cross-check `swap`, reject `?` bounds, and retain the harness medians without re-averaging.
5. Write `docs/benchmarks/YYYY-MM-DD-<label>.md` from the template with `ENV_VARS`, machine facts, branch/commit, conditions, and every valid resolution row. Do not commit it without approval.

For quick A/B work, call the same platform driver directly with one representative `--res`; do not invoke this skill.
