#!/usr/bin/env python3
"""anopticengine FPS / GPU-pass bench -- WINDOWS (win64) driver.

Methodology: tools/perf/bench_fps.md. This file: Windows window/DPI/foreground primitives.
Siblings: bench_fps_linux.py, bench_fps_macos.py.

Windows:
  - win32 window by PID, borderless resize, MoveWindow
  - forced + verified foreground (SetForegroundWindow focus-steal-lock workaround; background/occluded mismeasures GPU passes)
  - per-monitor-DPI-aware-v2: monitor rects/sizes are PHYSICAL pixels (scaled desktop otherwise reports logical sizes)
  - 'M' menu toggle as Win32 key message with real scancode

Log lines (logs/<stamp>_ano.log), flushed every ANO_PERF_WINDOW_FRAMES (128):
  [frame] <fps> fps <ms> ms wall
  [frametime] n=128 min= p50= p90= p99= p999= max= ms
  [profile mode=... res=WxH] total=<ms> (frusta N/42) ... swap=<MiB>
    res= = realized swapchain extent (render column). Older exes tabulate "?"

Sweep from measured primary monitor: ladder filtered, topped by display-native.
One row per point: avgFPS/p50 over [frame] samples, 1%/0.1% lows (1000/p99, 1000/p999, each median across [frametime] windows), maxms (run worst frame), then GPU-pass columns. Rows paste into docs/benchmarks/template.md.

Requires: Windows, pywin32. Dev-only.

Examples:
  python tools/perf/bench_fps_win64.py                          # resolution sweep, menu open
  python tools/perf/bench_fps_win64.py --res 3840x2160 --dur 60  # override the 30 s default
  python tools/perf/bench_fps_win64.py --no-menu                # static HUD only
  python tools/perf/bench_fps_win64.py --churn                  # resize-storm stress (one row)
  python tools/perf/bench_fps_win64.py --env ANO_SHADOW_BUDGET=0  # uncapped shadows (harness default caps at 2)
"""
import argparse, ctypes, os, re, subprocess, sys, time

# Per-monitor-DPI-aware v2 (-4) before win32: monitor rects are PHYSICAL px
# (otherwise a scaled desktop reports logical sizes and you mislabel the render resolution).
try: ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    try: ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception: pass
import win32gui, win32process, win32con, win32api  # noqa: E402

# Cross-machine ladder. Sweep derived per display in main().
LADDER = [(640, 360), (960, 540), (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160)]
CHURN_SIZES   = [(640, 480), (1920, 1080), (900, 1500), (2560, 1440), (480, 900),
                 (1600, 900), (1280, 720), (2200, 1300), (720, 1280), (1100, 1900)]
CHURN_MS = 33.0
WINDOW_FRAMES = 128  # ANO_PERF_WINDOW_FRAMES; frames per [frame]/[frametime]/[profile] window
WARMUP_S = 2.0       # leading [frame]/[frametime] seconds to discard

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


def _find_window(pid):
    hits = []
    def cb(h, _):
        if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1] == pid:
            hits.append(h)
    win32gui.EnumWindows(cb, None)
    return hits[0] if hits else None


def _bring_to_front(hwnd):
    """Defeat SetForegroundWindow lock (synthetic ALT), then confirm front.
    A background/occluded window mismeasures the GPU passes -- never trust a row that isn't front."""
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


def run_once(exe, w, h, dur, menu, churn, env):
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

    buf, part = [], ""
    resizes, nxt, f = 0, 0.0, None
    while (t := time.perf_counter() - t0) < dur:
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

    return summarize(*parse_stream(buf), front=front)


def main():
    ap = argparse.ArgumentParser(description="anopticengine wall-clock FPS / GPU-pass bench -- WINDOWS driver.")
    ap.add_argument("--exe", default=r"build\Release\anopticengine.exe")
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
    env = dict(os.environ)
    env.update(ENGINE_DEFAULTS)                  # harness defaults over ambient
    for kv in args.env:                          # --env wins over defaults
        k, _, v = kv.partition("="); env[k] = v
    ano = {k: env[k] for k in env if k.startswith("ANO_")}
    print("ENV_VARS: " + ", ".join(f"{k}={ano[k]}" for k in sorted(ano)))  # paste into bench template

    dw, dh = _display_px()
    print(f"display: primary monitor {dw}x{dh} px (physical; DPI-aware), the largest realizable framebuffer")

    if args.churn:
        sizes = [(dw, dh)]  # base res; run cycles CHURN_SIZES
    elif args.res:
        w, h = (int(x) for x in args.res.lower().split("x")); sizes = [(w, h)]
        if w > dw or h > dh:
            print(f"WARNING: {w}x{h} exceeds the {dw}x{dh} display; "
                  f"the render column has the truth", file=sys.stderr)
    else:
        sizes = [p for p in LADDER if p[0] <= dw and p[1] <= dh]
        dropped = [p for p in LADDER if p not in sizes]
        if (dw, dh) not in sizes:
            sizes.append((dw, dh))               # display-native point
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
    # Short run with no GPU profile: fail. Churn exempt (profile silent under resize storm).
    if short and not args.churn:
        sys.exit(f"ERROR: no GPU profile window survived warmup at {', '.join(short)} -- "
                 f"the run is too short for that point's fps; rerun with a longer --dur")


if __name__ == "__main__":
    main()
