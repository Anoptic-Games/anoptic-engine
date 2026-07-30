#!/usr/bin/env python3
"""anopticengine FPS / GPU-pass bench -- WINDOWS (win64) driver.

Methodology: tools/perf/bench_fps.md. This file: Windows window/DPI/foreground primitives.
Siblings: bench_fps_linux.py, bench_fps_macos.py.

Windows:
  - win32 window by PID, borderless resize, MoveWindow
  - forced + verified foreground (SetForegroundWindow focus-steal-lock workaround)
  - per-monitor-DPI-aware-v2: monitor rects/sizes are PHYSICAL pixels
  - 'M' menu toggle as Win32 key message with real scancode

Log lines (logs/<stamp>_ano.log), flushed every ANO_PERF_WINDOW_FRAMES (128):
  [frame] <fps> fps <ms> ms wall
  [frametime] n=128 min= p50= p90= p99= p999= max= ms
  [profile mode=... res=WxH] total=<ms> (frusta N/42) ... swap=<MiB>
    res= = realized swapchain extent (render column). Older exes tabulate "?"

Sweep: ladder filtered to primary monitor, topped by display-native.
Row: avgFPS/p50, 1%/0.1% lows (1000/p99, 1000/p999), maxms, GPU-pass columns. Paste into docs/benchmarks/template.md.

Requires: Windows, pywin32. Dev-only.

Examples:
  python tools/perf/bench_fps_win64.py                          # resolution sweep, menu open
  python tools/perf/bench_fps_win64.py --res 3840x2160 --dur 60  # override the 30 s default
  python tools/perf/bench_fps_win64.py --exe ref.exe --compare-exe variant.exe --res 2560x1440
  python tools/perf/bench_fps_win64.py --no-menu                # static HUD only
  python tools/perf/bench_fps_win64.py --churn                  # resize-storm stress (one row)
  python tools/perf/bench_fps_win64.py --env ANO_SHADOW_BUDGET=0  # uncapped shadows (harness default caps at 2)
"""
import argparse, ctypes, math, os, re, statistics, subprocess, sys, time

# Per-monitor-DPI-aware v2 (-4) before win32: monitor rects are PHYSICAL px.
try: ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    try: ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception: pass
import win32api, win32con, win32gui, win32pdh, win32pdhutil, win32process  # noqa: E402

# Cross-machine ladder. Sweep derived per display in main().
LADDER = [(640, 360), (960, 540), (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160)]
CHURN_SIZES   = [(640, 480), (1920, 1080), (900, 1500), (2560, 1440), (480, 900),
                 (1600, 900), (1280, 720), (2200, 1300), (720, 1280), (1100, 1900)]
CHURN_MS = 33.0
WINDOW_FRAMES = 128  # ANO_PERF_WINDOW_FRAMES; frames per [frame]/[frametime]/[profile] window
WARMUP_S = 2.0       # leading [frame]/[frametime] seconds to discard
COMPARE_PAIRS = 6    # even: equal A/B first-position occupancy

# Two-sided Student-t critical values at 95%; index = degrees of freedom.
T95 = (0.0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262,
       2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093,
       2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045,
       2.042)

# Defaults before --env. --env wins. Default: shadow-culled. =0 for uncapped.
ENGINE_DEFAULTS = {"ANO_SHADOW_BUDGET": "2"}

# Engine log contract (same regexes as linux).
PF = re.compile(r"\[frame\] ([0-9.]+) fps")
PT = re.compile(r"\[frametime\].*?p50=([0-9.]+) p90=([0-9.]+) p99=([0-9.]+) p999=([0-9.]+) max=([0-9.]+)")
PG = re.compile(r"total=([0-9.]+)")
PS = re.compile(r"swap=([0-9.]+)")
PR = re.compile(r"frusta ([0-9.]+)")
PX = re.compile(r"res=(\d+)x(\d+)")


def _display_px():
    """Primary monitor size in PHYSICAL pixels (DPI-aware v2 set above)."""
    return win32api.GetSystemMetrics(0), win32api.GetSystemMetrics(1)  # SM_CXSCREEN, SM_CYSCREEN


def _session_id(pid):
    sid = ctypes.c_ulong()
    try:
        if ctypes.windll.kernel32.ProcessIdToSessionId(pid, ctypes.byref(sid)):
            return sid.value
    except Exception:
        pass
    return None


def _find_window(pid):
    hits = []
    def cb(h, _):
        if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1] == pid:
            hits.append(h)
    win32gui.EnumWindows(cb, None)
    return hits[0] if hits else None


def _bring_to_front(hwnd):
    """Defeat SetForegroundWindow lock (synthetic ALT), then confirm front."""
    for _ in range(5):
        win32gui.ShowWindow(hwnd, win32con.SW_SHOW)
        win32api.keybd_event(0x12, 0, 0, 0)                            # ALT down
        win32api.keybd_event(0x12, 0, win32con.KEYEVENTF_KEYUP, 0)     # ALT up
        try: win32gui.SetForegroundWindow(hwnd)
        except Exception: pass
        win32gui.BringWindowToTop(hwnd)
        time.sleep(0.15)
        if win32gui.GetForegroundWindow() == hwnd:
            return True
    return win32gui.GetForegroundWindow() == hwnd


def _borderless(hwnd, x, y, w, h):
    st = win32gui.GetWindowLong(hwnd, win32con.GWL_STYLE)
    st = (st & ~win32con.WS_OVERLAPPEDWINDOW) | win32con.WS_POPUP | win32con.WS_VISIBLE
    win32gui.SetWindowLong(hwnd, win32con.GWL_STYLE, st)
    win32gui.SetWindowPos(hwnd, win32con.HWND_TOP, x, y, w, h,
                          win32con.SWP_FRAMECHANGED | win32con.SWP_SHOWWINDOW)


def _toggle_menu(hwnd):
    sc = 0x32  # scancode 'M'; GLFW maps by scancode, so lParam must carry it
    win32api.PostMessage(hwnd, win32con.WM_KEYDOWN, 0x4D, (sc << 16) | 1)
    time.sleep(0.03)
    win32api.PostMessage(hwnd, win32con.WM_KEYUP, 0x4D, (0xC0 << 24) | (sc << 16) | 1)


def _mean(a):
    return sum(a) / len(a) if a else 0.0


def _pct(a, q):
    # Linear-interp percentile, q in [0, 100].
    if not a: return 0.0
    s = sorted(a)
    if len(s) == 1: return s[0]
    idx = (q / 100.0) * (len(s) - 1)
    lo = int(idx); hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (idx - lo)


def _median(a):
    return _pct(a, 50.0)


def _mean_ci95(values):
    """Student-t 95% confidence interval for independent observations."""
    center = _mean(values)
    if len(values) < 2:
        return center, None, None
    df = len(values) - 1
    critical = T95[df] if df < len(T95) else 1.960
    half = critical * statistics.stdev(values) / math.sqrt(len(values))
    return center, center - half, center + half


def _balanced_pair_orders(pairs):
    """Adjacent pairs in ABBA/BAAB blocks; A and B occupy first position equally."""
    if pairs < 2 or pairs % 2:
        raise ValueError("comparison pairs must be an even integer >= 2")
    cycle = (("A", "B"), ("B", "A"), ("B", "A"), ("A", "B"))
    return [cycle[i % len(cycle)] for i in range(pairs)]


def paired_summary(pairs):
    """Fold adjacent (A, B) results into the paired B-A estimate and 95% CI."""
    if len(pairs) < 2:
        raise ValueError("paired comparison needs at least two pairs")
    afps = [a["avg_fps"] for a, _ in pairs]
    bfps = [b["avg_fps"] for _, b in pairs]
    mean_a, mean_b = _mean(afps), _mean(bfps)
    diff, low, high = _mean_ci95([b - a for a, b in zip(afps, bfps)])
    scale = 100.0 / mean_a if mean_a else 0.0
    adwm = [a["dwm_gpu"] for a, _ in pairs if a.get("dwm_gpu") is not None]
    bdwm = [b["dwm_gpu"] for _, b in pairs if b.get("dwm_gpu") is not None]
    return {"a_fps": mean_a, "b_fps": mean_b, "delta_pct": diff * scale,
            "ci_low_pct": low * scale if low is not None else None,
            "ci_high_pct": high * scale if high is not None else None,
            "a_dwm": _mean(adwm) if len(adwm) == len(pairs) else None,
            "b_dwm": _mean(bdwm) if len(bdwm) == len(pairs) else None}


def _warmup_cut(fps, seconds=WARMUP_S):
    # Cut by elapsed time (window cadence scales with fps).
    t, k = 0.0, 0
    while k < len(fps) and t < seconds:
        t += (WINDOW_FRAMES / fps[k]) if fps[k] > 0 else seconds
        k += 1
    return k


def parse_stream(lines):
    """Fold log lines into (fps, tot, swap, frusta, frametime, res) lists."""
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
    """Drop warmup, medians, GPUcap, bound, frametime lows. See bench_fps.md."""
    cut = _warmup_cut(fps)                       # [frame]/[frametime] by time, profile by line
    fps, tot, fru = fps[cut:], tot[4:], fru[4:]
    ft = {k: v[cut:] for k, v in ft.items()}     # [frametime] 1:1 with [frame]
    wf, gt = _median(fps), _median(tot)
    cap = 1000.0 / gt if gt else 0.0
    ratio = wf / cap if cap else 0.0
    # Lows: median of per-window percentiles, then 1000/ms. maxms = run worst frame.
    p99, p999 = _median(ft["p99"]), _median(ft["p999"])
    return {"front": front, "swap": (sw[-1] if sw else 0.0),
            "res": (res[-1] if res else None),   # None on pre-res= exes
            "avg_fps": _mean(fps), "p50": wf, "n": len(fps), "n_ft": len(ft["p99"]),
            "low1": (1000.0 / p99 if p99 else 0.0), "low01": (1000.0 / p999 if p999 else 0.0),
            "ft_max": (max(ft["max"]) if ft["max"] else 0.0),
            "gpu_ms": gt, "gpu_cap": cap, "ratio": ratio, "frusta": _median(fru),
            # No profile past cut: "?"
            "bound": ("GPU" if ratio > 0.9 else "CPU/present") if gt else "?"}


class _DwmGpuSampler:
    """One-Hz aggregate utilization across DWM's Windows GPU Engine instances."""
    def __init__(self):
        self.query, self.counters, self.values = None, [], []
        self.next_sample = WARMUP_S
        try:
            pids = win32pdhutil.FindPerformanceAttributesByName("dwm", bRefresh=1)
            if not pids:
                return
            own_session = _session_id(os.getpid())
            pid = next((p for p in pids if _session_id(p) == own_session), pids[0])
            needle = f"(pid_{pid}_"
            obj = win32pdhutil.find_pdh_counter_localized_name("GPU Engine")
            counter = win32pdhutil.find_pdh_counter_localized_name("Utilization Percentage")
            paths = [p for p in win32pdh.ExpandCounterPath(
                f"\\{obj}(*)\\{counter}") if needle in p.lower()]
            if not paths:
                return
            self.query = win32pdh.OpenQuery()
            self.counters = [win32pdh.AddCounter(self.query, p) for p in paths]
            win32pdh.CollectQueryData(self.query)  # rate counters need a prime
        except Exception:
            self.close()

    def poll(self, elapsed):
        if self.query is None or elapsed < self.next_sample:
            return
        self.next_sample = elapsed + 1.0
        try:
            win32pdh.CollectQueryData(self.query)
            values = []
            for counter in self.counters:
                try:
                    values.append(win32pdh.GetFormattedCounterValue(
                        counter, win32pdh.PDH_FMT_DOUBLE)[1])
                except Exception:
                    pass
            if values:
                self.values.append(sum(values))
        except Exception:
            pass

    def close(self):
        if self.query is not None:
            try: win32pdh.CloseQuery(self.query)
            except Exception: pass
        self.query = None

    def finish(self):
        value, count = (_mean(self.values), len(self.values)) if self.values else (None, 0)
        self.close()
        return value, count


def run_once(exe, w, h, dur, menu, churn, env, measure_dwm=False):
    # logs/<session-stamp>_ano.log. Snapshot preexisting, pick up the new file.
    logdir = os.path.join(os.path.dirname(exe), "logs")
    def _logfiles():
        try: return {os.path.join(logdir, n) for n in os.listdir(logdir) if n.endswith("_ano.log")}
        except FileNotFoundError: return set()
    pre = _logfiles()

    p = subprocess.Popen([exe], env=env)
    t0 = time.perf_counter()
    log = None
    hwnd = None
    while time.perf_counter() - t0 < 15 and not hwnd:
        hwnd = _find_window(p.pid); time.sleep(0.1)

    front = False
    if hwnd:
        _borderless(hwnd, 0, 0, w, h)
        front = _bring_to_front(hwnd)
        if menu:
            _toggle_menu(hwnd)

    dwm = _DwmGpuSampler() if measure_dwm else None
    buf, part = [], ""
    resizes, nxt, f = 0, 0.0, None
    while (t := time.perf_counter() - t0) < dur:
        if dwm:
            dwm.poll(t)
        if churn and hwnd and t >= nxt:
            cw, ch = CHURN_SIZES[resizes % len(CHURN_SIZES)]
            try: win32gui.MoveWindow(hwnd, 0, 0, cw, ch, True)
            except Exception: pass
            resizes += 1; nxt = resizes * (CHURN_MS / 1000.0)
        if f is None:
            if log is None:                      # newest log this process created
                fresh = _logfiles() - pre
                if fresh: log = max(fresh, key=os.path.getmtime)
                else: time.sleep(0.01); continue
            if os.path.exists(log): f = open(log, encoding="utf-8", errors="replace")
            else: time.sleep(0.01); continue
        chunk = f.readline()
        if not chunk: time.sleep(0.003); continue
        part += chunk
        if not part.endswith("\n"): continue    # torn mid-append
        buf.append(part); part = ""

    subprocess.run(["taskkill", "/PID", str(p.pid), "/F"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try: p.wait(timeout=5)
    except Exception: pass

    result = summarize(*parse_stream(buf), front=front)
    result["dwm_gpu"], result["dwm_n"] = dwm.finish() if dwm else (None, 0)
    return result


def _sizes(args, dw, dh):
    if args.churn:
        return [(dw, dh)]
    if args.res:
        w, h = (int(x) for x in args.res.lower().split("x"))
        if w > dw or h > dh:
            print(f"WARNING: {w}x{h} exceeds the {dw}x{dh} display; "
                  f"the render column has the truth", file=sys.stderr)
        return [(w, h)]
    sizes = [p for p in LADDER if p[0] <= dw and p[1] <= dh]
    dropped = [p for p in LADDER if p not in sizes]
    if (dw, dh) not in sizes:
        sizes.append((dw, dh))
    if dropped:
        print("sweep: dropped " + ", ".join(f"{w}x{h}" for w, h in dropped)
              + f" (exceed this display); display native {dw}x{dh} tops the sweep")
    return sizes


def _result_row(label, result, prefix="", show_dwm=False):
    rr = f"{result['res'][0]}x{result['res'][1]}" if result["res"] else "?"
    dwm = f"{result['dwm_gpu']:7.2f}" if result.get("dwm_gpu") is not None else f"{'?':>7}"
    row = (f"{prefix}{label:>11} {'FRONT' if result['front'] else 'BG!!':>5} {rr:>11} "
           f"{result['swap']:8.1f} {result['avg_fps']:7.1f} {result['p50']:7.1f} "
           f"{result['low1']:7.1f} {result['low01']:7.1f} {result['ft_max']:7.3f} "
           f"{result['gpu_ms']:7.3f} {result['gpu_cap']:7.0f} {result['ratio']:6.2f} "
           f"{result['frusta']:6.1f}  {result['bound']}")
    print(row + (f" {dwm}" if show_dwm else ""))


def _result_header(prefix="", show_dwm=False):
    row = (f"{prefix}{'target':>11} {'front':>5} {'render':>11} {'swapMiB':>8} "
           f"{'avgFPS':>7} {'p50':>7} {'1%low':>7} {'0.1%low':>7} {'maxms':>7} "
           f"{'GPUms':>7} {'GPUcap':>7} {'w/cap':>6} {'frusta':>6}  bound")
    print(row + (f" {'DWM%':>7}" if show_dwm else ""))


def _validate_comparison(pairs, label):
    errors = []
    for i, (a, b) in enumerate(pairs, 1):
        for revision, result in (("A", a), ("B", b)):
            if not result["front"]:
                errors.append(f"{label} pair {i} {revision} was not foreground")
            if result["bound"] == "?":
                errors.append(f"{label} pair {i} {revision} has no GPU profile after warmup")
        if a["res"] != b["res"]:
            errors.append(f"{label} pair {i} render mismatch: A={a['res']} B={b['res']}")
    return errors


def _run_comparison(args, exe_a, exe_b, sizes, env):
    orders = _balanced_pair_orders(args.pairs)
    expected = len(sizes) * args.pairs * 2 * args.dur / 60.0
    print(f"A: {exe_a}\nB: {exe_b}")
    print(f"comparison: {args.pairs} adjacent pairs per resolution, ABBA/BAAB balanced order, "
          f"Student-t 95% CI, aggregate DWM GPU exposure; about {expected:.1f} minutes")
    _result_header(f"{'pair':>5} {'ord':>3} {'rev':>3} ", show_dwm=True)
    summaries, errors = [], []
    for w, h in sizes:
        label = "churn" if args.churn else f"{w}x{h}"
        paired = []
        for pair_index, order in enumerate(orders, 1):
            results = {}
            for revision in order:
                exe = exe_a if revision == "A" else exe_b
                result = run_once(exe, w, h, args.dur, not args.no_menu,
                                  args.churn, env, measure_dwm=True)
                results[revision] = result
                _result_row(label, result,
                            f"{pair_index:5d} {''.join(order):>3} {revision:>3} ",
                            show_dwm=True)
            paired.append((results["A"], results["B"]))
        errors.extend(_validate_comparison(paired, label))
        summaries.append((label, paired_summary(paired)))

    print("\npaired summary: B - A; 95% CI over adjacent-pair FPS differences")
    print(f"{'target':>11} {'A FPS':>9} {'B FPS':>9} {'delta':>9} {'paired 95% CI':>20} "
          f"{'A DWM%':>8} {'B DWM%':>8}  verdict")
    for label, summary in summaries:
        low, high = summary["ci_low_pct"], summary["ci_high_pct"]
        ci = f"[{low:+.2f}%, {high:+.2f}%]"
        if low > 0.0:
            verdict = "B faster"
        elif high < 0.0:
            verdict = "B slower"
        else:
            verdict = "neutral"
        adwm = f"{summary['a_dwm']:.2f}" if summary["a_dwm"] is not None else "?"
        bdwm = f"{summary['b_dwm']:.2f}" if summary["b_dwm"] is not None else "?"
        print(f"{label:>11} {summary['a_fps']:9.2f} {summary['b_fps']:9.2f} "
              f"{summary['delta_pct']:+8.2f}% {ci:>20} {adwm:>8} {bdwm:>8}  {verdict}")
    if any(summary["a_dwm"] is None or summary["b_dwm"] is None
           for _, summary in summaries):
        errors.append("DWM GPU counters were unavailable for one or more comparison runs")
    if errors:
        sys.exit("ERROR: invalid comparison:\n  " + "\n  ".join(errors))


def main():
    ap = argparse.ArgumentParser(description="anopticengine wall-clock FPS / GPU-pass bench -- WINDOWS driver.")
    ap.add_argument("--exe", default=r"build\Release\anopticengine.exe")
    ap.add_argument("--compare-exe",
                    help="B executable for a paired A/B comparison; --exe is A")
    ap.add_argument("--pairs", type=int, default=COMPARE_PAIRS,
                    help=f"adjacent A/B pairs per resolution (even; default: {COMPARE_PAIRS})")
    ap.add_argument("--res", help="single WxH, e.g. 1920x1080 (default: resolution sweep)")
    ap.add_argument("--dur", type=float, default=30.0,
                    help="seconds per data point (default: exactly 30)")
    ap.add_argument("--no-menu", action="store_true", help="static HUD only (no menu open)")
    ap.add_argument("--churn", action="store_true", help="resize-storm stress; single row")
    ap.add_argument("--env", action="append", default=[],
                    help="KEY=VAL engine env var (repeatable); overrides defaults. "
                         "Default caps ANO_SHADOW_BUDGET=2; pass =0 for the uncapped path.")
    args = ap.parse_args()

    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        sys.exit(f"exe not found: {exe} (build it, e.g. build.bat 1)")
    compare_exe = os.path.abspath(args.compare_exe) if args.compare_exe else None
    if compare_exe and not os.path.exists(compare_exe):
        sys.exit(f"compare exe not found: {compare_exe}")
    if compare_exe:
        if args.churn:
            sys.exit("ERROR: --compare-exe cannot be combined with --churn")
        try: _balanced_pair_orders(args.pairs)
        except ValueError as e: sys.exit(f"ERROR: {e}")
    env = dict(os.environ)
    env.update(ENGINE_DEFAULTS)                  # harness defaults over ambient
    for kv in args.env:                          # --env wins over defaults
        k, _, v = kv.partition("="); env[k] = v
    ano = {k: env[k] for k in env if k.startswith("ANO_")}
    print("ENV_VARS: " + ", ".join(f"{k}={ano[k]}" for k in sorted(ano)))  # paste into bench template

    dw, dh = _display_px()
    print(f"display: primary monitor {dw}x{dh} px (physical; DPI-aware), the largest realizable framebuffer")
    sizes = _sizes(args, dw, dh)
    if compare_exe:
        _run_comparison(args, exe, compare_exe, sizes, env)
        return

    _result_header()
    short = []
    for (w, h) in sizes:
        r = run_once(exe, w, h, args.dur, not args.no_menu, args.churn, env)
        label = "churn" if args.churn else f"{w}x{h}"
        _result_row(label, r)
        if r["bound"] == "?":
            short.append(label)
    # Short run with no GPU profile: fail. Churn exempt (profile silent under resize storm).
    if short and not args.churn:
        sys.exit(f"ERROR: no GPU profile window survived warmup at {', '.join(short)} -- "
                 f"the run is too short for that point's fps; rerun with a longer --dur")


if __name__ == "__main__":
    main()
