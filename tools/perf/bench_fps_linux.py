#!/usr/bin/env python3
"""anopticengine FPS / GPU-pass benchmark harness -- LINUX (X11/Xwayland) DRIVER.

Sibling of bench_fps_win64.py. Methodology and the engine log contract live in
tools/perf/bench_fps.md. This file implements that contract with Linux window primitives.

Linux-specific:
  - window discovery by PID via xdotool search --pid (_NET_WM_PID), name fallback
  - forced + verified foreground via xdotool windowactivate --sync vs getactivewindow
  - borderless exact-size render surface via _MOTIF_WM_HINTS strip + xdotool windowsize --sync
  - 'M' menu toggle via XTEST (xdotool key) to the focused window

Parsed engine log lines (logs/<stamp>_ano.log, same on every target), each flushed every
ANO_PERF_WINDOW_FRAMES (128) frames, so line cadence scales with fps:
  [frame] <fps> fps <ms> ms wall            -- wall-clock throughput (profiling.c: anoperf_flush)
  [frametime] n=128 min= p50= p90= p99= p999= max= ms  -- per-frame dt percentiles, same window
  [profile mode=... res=WxH] total=<ms> (frusta N/42) ... swap=<MiB>  -- GPU-pass profile + VRAM;
    res= is the realized swapchain extent, tabulated as the render column (ground truth against
    the target label; exes older than the res= addition tabulate "?")

The sweep is derived from the measured display, never a hardcoded ladder: the standard ladder
is filtered to points the X screen can realize, topped by the display-native point.

One table row per data point: avgFPS/p50 over the per-window [frame] samples, the 1%/0.1%
lows (1000/p99, 1000/p999, each percentile the median across [frametime] windows), the run's
worst single frame (maxms), then the GPU-pass columns. Rows paste straight into
docs/benchmarks/template.md.

X11 hands out device pixels and the driver commands exact pixel window sizes. The render
column (engine res=) names the rendered resolution when comparing machines; swap= cross-checks.

Tools via NIX, never apt: xdotool (required), wmctrl + xprop (optional). e.g.:
  nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop nixpkgs#xorg.xrandr
Drives X11/Xwayland clients only, never native-Wayland GLFW windows.

Requires: Linux, Python 3, an X server. Dev-only tool, not built or shipped.

Examples:
  python3 tools/perf/bench_fps_linux.py                          # resolution sweep, menu open
  python3 tools/perf/bench_fps_linux.py --res 3840x2160 --dur 60  # override the 30 s default
  python3 tools/perf/bench_fps_linux.py --no-menu                # static HUD only
  python3 tools/perf/bench_fps_linux.py --churn                  # resize-storm stress (one row)
  python3 tools/perf/bench_fps_linux.py --env ANO_SHADOW_BUDGET=0  # uncapped shadows (harness default caps at 2)
"""
import argparse, os, re, shutil, subprocess, sys, time

# Standard cross-machine ladder. The actual sweep is derived per display in main():
# ladder points the X screen can realize, topped by the display-native point.
LADDER = [(640, 360), (960, 540), (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160)]
CHURN_SIZES   = [(640, 480), (1920, 1080), (900, 1500), (2560, 1440), (480, 900),
                 (1600, 900), (1280, 720), (2200, 1300), (720, 1280), (1100, 1900)]
CHURN_MS = 33.0  # target floor, real cadence a little slower
WINDOW_FRAMES = 128  # engine ANO_PERF_WINDOW_FRAMES; frames per [frame]/[frametime]/[profile] window
WARMUP_S = 2.0       # leading seconds of [frame]/[frametime] windows to discard

# Engine env applied to every run before --env; --env wins per key. The bench measures the
# shadow-culled path by default -- pass --env ANO_SHADOW_BUDGET=0 for the uncapped baseline.
ENGINE_DEFAULTS = {"ANO_SHADOW_BUDGET": "2"}

# Engine log contract, same regexes as the win64 driver.
PF = re.compile(r"\[frame\] ([0-9.]+) fps")
PT = re.compile(r"\[frametime\].*?p50=([0-9.]+) p90=([0-9.]+) p99=([0-9.]+) p999=([0-9.]+) max=([0-9.]+)")
PG = re.compile(r"total=([0-9.]+)")
PS = re.compile(r"swap=([0-9.]+)")
PR = re.compile(r"frusta ([0-9.]+)")
PX = re.compile(r"res=(\d+)x(\d+)")


def _display_px():
    """X screen size in device pixels via xdotool getdisplaygeometry. X11 hands out device
    pixels, so this is also the largest realizable framebuffer. Caveat: a multi-monitor X
    screen reports the combined desktop; the driver still places windows at 0,0, so the
    per-monitor bound is the leftmost head. (0, 0) when the query fails."""
    out = _run(["xdotool", "getdisplaygeometry"]).split()
    return (int(out[0]), int(out[1])) if len(out) == 2 else (0, 0)


def _run(cmd):
    """Run a helper tool, return stripped stdout ('' on any failure). Never raises."""
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=5).stdout.strip()
    except Exception:
        return ""


def _active_window():
    return _run(["xdotool", "getactivewindow"])


def _find_window(pid):
    """Window id for pid: _NET_WM_PID (xdotool --pid), pid-checked 'Vulkan'-title fallback.
    Decimal ids throughout (same space as getactivewindow)."""
    ids = [w for w in _run(["xdotool", "search", "--onlyvisible", "--pid", str(pid)]).split() if w]
    if not ids:
        for w in _run(["xdotool", "search", "--name", "Vulkan"]).split():
            if _run(["xdotool", "getwindowpid", w]) == str(pid):
                ids = [w]
                break
    return ids[0] if ids else None


def _strip_decorations(wid):
    # Motif hint: decorations=0 -> borderless, windowsize == render size.
    if shutil.which("xprop"):
        _run(["xprop", "-id", wid, "-f", "_MOTIF_WM_HINTS", "32c",
              "-set", "_MOTIF_WM_HINTS", "2, 0, 0, 0, 0"])


def _resize(wid, w, h, sync):
    cmd = ["xdotool", "windowsize"] + (["--sync"] if sync else []) + [wid, str(w), str(h)]
    _run(cmd)
    _run(["xdotool", "windowmove", wid, "0", "0"])


def _bring_to_front(wid):
    """Force the window to the foreground and confirm it."""
    have_wmctrl = shutil.which("wmctrl") is not None
    for _ in range(5):
        _run(["xdotool", "windowactivate", "--sync", wid])
        _run(["xdotool", "windowraise", wid])
        if have_wmctrl:
            _run(["wmctrl", "-i", "-a", wid])
        time.sleep(0.15)
        if _active_window() == wid:
            return True
    return _active_window() == wid


def _toggle_menu(wid):
    # Focus, then XTEST 'm' (GLFW ignores XSendEvent synthetics).
    _run(["xdotool", "windowfocus", "--sync", wid])
    if _run(["xdotool", "getwindowfocus"]) != wid:
        return  # focus refused: skip
    _run(["xdotool", "key", "--clearmodifiers", "m"])


def _mean(a):
    return sum(a) / len(a) if a else 0.0


def _pct(a, q):
    # Linear-interpolation percentile (numpy-default method), q in [0, 100].
    if not a: return 0.0
    s = sorted(a)
    if len(s) == 1: return s[0]
    idx = (q / 100.0) * (len(s) - 1)
    lo = int(idx); hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (idx - lo)


def _median(a):
    return _pct(a, 50.0)


def _warmup_cut(fps, seconds=WARMUP_S):
    # Windows are WINDOW_FRAMES long, so cadence scales with fps: cut warmup by elapsed time, not line count.
    t, k = 0.0, 0
    while k < len(fps) and t < seconds:
        t += (WINDOW_FRAMES / fps[k]) if fps[k] > 0 else seconds
        k += 1
    return k


def parse_stream(lines):
    """Fold engine log lines into (fps, total_ms, swap_MiB, frusta, frametime, res) sample lists.
    Shared by the live tail and offline replay of a captured _ano.log."""
    fps, tot, sw, fru, res = [], [], [], [], []
    ft = {"p50": [], "p90": [], "p99": [], "p999": [], "max": []}
    for line in lines:
        m = PF.search(line);  m and fps.append(float(m.group(1)))
        mt = PT.search(line)
        if mt:
            ft["p50"].append(float(mt.group(1)));  ft["p90"].append(float(mt.group(2)))
            ft["p99"].append(float(mt.group(3)));  ft["p999"].append(float(mt.group(4)))
            ft["max"].append(float(mt.group(5)))
        if "profile mode=" in line:
            g = PG.search(line); g and tot.append(float(g.group(1)))
            s = PS.search(line); s and sw.append(float(s.group(1)))
            r = PR.search(line); r and fru.append(float(r.group(1)))
            x = PX.search(line); x and res.append((int(x.group(1)), int(x.group(2))))
    return fps, tot, sw, fru, ft, res


def summarize(fps, tot, sw, fru, ft, res, front=True):
    """Drop warmup, take medians, derive GPUcap, the wall/cap bound indicator, and the
    frametime lows (methodology: tools/perf/bench_fps.md)."""
    cut = _warmup_cut(fps)                       # drop warmup: [frame]/[frametime] by time, profile by line
    fps, tot, fru = fps[cut:], tot[4:], fru[4:]
    ft = {k: v[cut:] for k, v in ft.items()}     # [frametime] pairs 1:1 with [frame]; same cut
    wf, gt = _median(fps), _median(tot)
    cap = 1000.0 / gt if gt else 0.0
    ratio = wf / cap if cap else 0.0
    # Lows: median across windows of each per-window percentile (never averaged), then 1000/ms.
    # At n=128 p999 reads as the typical worst-frame-per-window; maxms is the run's worst frame outright.
    p99, p999 = _median(ft["p99"]), _median(ft["p999"])
    return {"front": front, "swap": (sw[-1] if sw else 0.0),
            "res": (res[-1] if res else None),   # realized swapchain extent; None on pre-res= exes
            "avg_fps": _mean(fps), "p50": wf, "n": len(fps), "n_ft": len(ft["p99"]),
            "low1": (1000.0 / p99 if p99 else 0.0), "low01": (1000.0 / p999 if p999 else 0.0),
            "ft_max": (max(ft["max"]) if ft["max"] else 0.0),
            "gpu_ms": gt, "gpu_cap": cap, "ratio": ratio, "frusta": _median(fru),
            # No profile lines past the cut (run too short at low fps): "?" -- never claim a bound.
            "bound": ("GPU" if ratio > 0.9 else "CPU/present") if gt else "?"}


def run_once(exe, w, h, dur, menu, churn, env):
    # Logging refactor (b85e213): each run writes logs/<session-stamp>_ano.log, no fixed anoptic.log.
    # Snapshot preexisting logs, then pick up whichever new file this process opens.
    logdir = os.path.join(os.path.dirname(exe), "logs")
    def _logfiles():
        try: return {os.path.join(logdir, n) for n in os.listdir(logdir) if n.endswith("_ano.log")}
        except FileNotFoundError: return set()
    pre = _logfiles()

    p = subprocess.Popen([exe], env=env)
    t0 = time.perf_counter()
    log = None
    wid = None
    while time.perf_counter() - t0 < 15 and not wid:
        wid = _find_window(p.pid); time.sleep(0.1)

    front = False
    if wid:
        _strip_decorations(wid)
        _resize(wid, w, h, sync=True)
        front = _bring_to_front(wid)
        if menu:
            _toggle_menu(wid)

    buf, part = [], ""
    resizes, nxt, f = 0, 0.0, None
    while (t := time.perf_counter() - t0) < dur:
        if churn and wid and t >= nxt:
            cw, ch = CHURN_SIZES[resizes % len(CHURN_SIZES)]
            _resize(wid, cw, ch, sync=False)
            resizes += 1; nxt = resizes * (CHURN_MS / 1000.0)
        if f is None:
            if log is None:                      # newest log file this process created
                fresh = _logfiles() - pre
                if fresh: log = max(fresh, key=os.path.getmtime)
                else: time.sleep(0.01); continue
            if os.path.exists(log): f = open(log, encoding="utf-8", errors="replace")
            else: time.sleep(0.01); continue
        chunk = f.readline()
        if not chunk: time.sleep(0.003); continue
        part += chunk
        if not part.endswith("\n"): continue    # torn mid-append: wait for the rest of the line
        buf.append(part); part = ""

    p.terminate()
    try: p.wait(timeout=5)
    except Exception: p.kill()

    return summarize(*parse_stream(buf), front=front)


def main():
    ap = argparse.ArgumentParser(description="anopticengine wall-clock FPS / GPU-pass bench -- LINUX driver.")
    ap.add_argument("--exe", default="build/Release/anopticengine")
    ap.add_argument("--res", help="single WxH, e.g. 1920x1080 (default: resolution sweep)")
    ap.add_argument("--dur", type=float, default=30.0,
                    help="seconds per data point (default: exactly 30)")
    ap.add_argument("--no-menu", action="store_true", help="static HUD only (no menu open)")
    ap.add_argument("--churn", action="store_true", help="resize-storm stress; single row")
    ap.add_argument("--env", action="append", default=[],
                    help="KEY=VAL engine env var (repeatable); overrides defaults. "
                         "Default caps ANO_SHADOW_BUDGET=2; pass =0 for the uncapped path.")
    args = ap.parse_args()

    if shutil.which("xdotool") is None:
        sys.exit("xdotool not found. Bring it in with NIX (never apt), e.g.:\n"
                 "  nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop")
    if os.environ.get("WAYLAND_DISPLAY") and not os.environ.get("DISPLAY"):
        print("WARNING: pure Wayland session (no DISPLAY) -- xdotool cannot drive arbitrary windows.\n"
              "         Run the engine as an X11/Xwayland client (X server + DISPLAY set).", file=sys.stderr)

    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        sys.exit(f"exe not found: {exe} (build it, e.g. the Release preset)")
    env = dict(os.environ)
    # Hide WAYLAND_DISPLAY: the engine comes up as an Xwayland client (--env can restore it).
    env.pop("WAYLAND_DISPLAY", None)
    env.update(ENGINE_DEFAULTS)                  # harness defaults over ambient
    for kv in args.env:                          # --env wins over defaults
        k, _, v = kv.partition("="); env[k] = v
    ano = {k: env[k] for k in env if k.startswith("ANO_")}
    print("ENV_VARS: " + ", ".join(f"{k}={ano[k]}" for k in sorted(ano)))  # paste into the bench template

    dw, dh = _display_px()
    if dw:
        print(f"display: X screen {dw}x{dh} px (device pixels), the largest realizable framebuffer")
    else:
        print("WARNING: display size query failed; sweeping the unfiltered ladder, trust the render column",
              file=sys.stderr)

    if args.churn:
        sizes = [(dw, dh) if dw else (3840, 2160)]  # base res, the run cycles CHURN_SIZES
    elif args.res:
        w, h = (int(x) for x in args.res.lower().split("x")); sizes = [(w, h)]
        if dw and (w > dw or h > dh):
            print(f"WARNING: {w}x{h} exceeds the {dw}x{dh} display; "
                  f"the render column has the truth", file=sys.stderr)
    elif not dw:
        sizes = LADDER
    else:
        sizes = [p for p in LADDER if p[0] <= dw and p[1] <= dh]
        dropped = [p for p in LADDER if p not in sizes]
        if (dw, dh) not in sizes:
            sizes.append((dw, dh))               # display-native point: the full-desktop datum
        if dropped:
            print("sweep: dropped " + ", ".join(f"{w}x{h}" for w, h in dropped)
                  + f" (exceed this display); display native {dw}x{dh} tops the sweep")

    print(f"{'target':>11} {'front':>5} {'render':>11} {'swapMiB':>8} {'avgFPS':>7} {'p50':>7} "
          f"{'1%low':>7} {'0.1%low':>7} {'maxms':>7} {'GPUms':>7} {'GPUcap':>7} {'w/cap':>6} "
          f"{'frusta':>6}  bound")
    short = []
    for (w, h) in sizes:
        r = run_once(exe, w, h, args.dur, not args.no_menu, args.churn, env)
        label = "churn" if args.churn else f"{w}x{h}"
        rr = f"{r['res'][0]}x{r['res'][1]}" if r["res"] else "?"
        print(f"{label:>11} {'FRONT' if r['front'] else 'BG!!':>5} {rr:>11} {r['swap']:8.1f} "
              f"{r['avg_fps']:7.1f} {r['p50']:7.1f} {r['low1']:7.1f} {r['low01']:7.1f} "
              f"{r['ft_max']:7.3f} {r['gpu_ms']:7.3f} {r['gpu_cap']:7.0f} {r['ratio']:6.2f} "
              f"{r['frusta']:6.1f}  {r['bound']}")
        if r["bound"] == "?":
            short.append(label)
    # A run too short for GPU profile windows never passes silently. Churn is exempt: the
    # profile line is GPU-timestamp-gated and goes silent under a resize storm by design.
    if short and not args.churn:
        sys.exit(f"ERROR: no GPU profile window survived warmup at {', '.join(short)} -- "
                 f"the run is too short for that point's fps; rerun with a longer --dur")


if __name__ == "__main__":
    main()
