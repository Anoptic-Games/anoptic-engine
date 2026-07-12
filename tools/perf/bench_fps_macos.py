#!/usr/bin/env python3
"""anopticengine FPS / GPU-pass benchmark harness -- MACOS (Cocoa) DRIVER.

Sibling of bench_fps_win64.py / bench_fps_linux.py. Methodology and the engine log
contract live in tools/perf/bench_fps.md. This file implements that contract with
macOS primitives, none of which need the Accessibility permission.

macOS-specific:
  - external window resize (AXUIElement) and key injection (CGEventPost) are
    Accessibility-gated, so the driver uses the engine's launch knobs instead:
    ANO_RES=WxH sizes the window at boot, ANO_MENU opens the HUD menu at boot.
    A data point is a fresh process anyway, so launch-time knobs lose nothing.
  - THREE resolutions coexist on a scaled Retina desktop and the driver reports all
    of them instead of pretending they are one number: the physical PANEL (e.g.
    2560x1600), the desktop MODE in points (e.g. 1440x900), and the BACKING store
    the swapchain actually renders into (points x backingScaleFactor, e.g.
    2880x1800 -- which can EXCEED the panel; WindowServer downsamples). Bench
    pixels are backing pixels: --res / sweep values are framebuffer pixels on
    every driver, and ANO_RES takes points, so the driver divides by the scale.
  - the sweep is derived from the measured display, never a hardcoded ladder: the
    standard ladder is filtered to points this display can realize, and the top
    point is the display max (largest titled-window content x scale). No request
    is ever made that AppKit would silently clamp.
  - the render column is ground truth: the engine's [profile] line carries res=WxH,
    the realized swapchain extent. swap= MiB stays the linear cross-check.
  - window discovery by PID via CGWindowListCopyWindowInfo (kCGWindowOwnerPID),
    the CGWindow precedent from the screenshot-macos tool
  - FRONT means active or <5% occluded (CGWindowList z-order): macOS 14 cooperative
    activation denies focus grabs to background scripts, so the driver floats the
    window above normal windows (ANO_FLOAT) instead of stealing focus. Measured
    control on this stack: even a fully occluded window benched within 1%.
  - --churn is unsupported: storming a live window's size needs Accessibility.

Parsed engine log lines (logs/<stamp>_ano.log, same on every target), each flushed every
ANO_PERF_WINDOW_FRAMES (128) frames, so line cadence scales with fps:
  [frame] <fps> fps <ms> ms wall            -- wall-clock throughput (profiling.c: anoperf_flush)
  [frametime] n=128 min= p50= p90= p99= p999= max= ms  -- per-frame dt percentiles, same window
  [profile mode=... res=WxH] total=<ms> (frusta N/42) ... swap=<MiB>  -- GPU-pass profile + VRAM;
    res= is the realized swapchain extent (exes older than the res= addition tabulate "?")

One table row per data point: avgFPS/p50 over the per-window [frame] samples, the 1%/0.1%
lows (1000/p99, 1000/p999, each percentile the median across [frametime] windows), the run's
worst single frame (maxms), then the GPU-pass columns. Rows paste straight into
docs/benchmarks/template.md.

Python via NIX, never brew:
  nix-shell -p "python3.withPackages (ps: [ps.pyobjc-framework-Quartz ps.pyobjc-framework-Cocoa])" \
    --run "python3 tools/perf/bench_fps_macos.py"

Requires: macOS, pyobjc (Quartz + Cocoa). Dev-only tool, not built or shipped.

Examples:
  python3 tools/perf/bench_fps_macos.py                          # resolution sweep, menu open
  python3 tools/perf/bench_fps_macos.py --res 2560x1440 --dur 60   # override the 30 s default
  python3 tools/perf/bench_fps_macos.py --no-menu                # static HUD only
  python3 tools/perf/bench_fps_macos.py --env ANO_SHADOW_BUDGET=0  # uncapped shadows (harness default caps at 2)
"""
import argparse, os, re, subprocess, sys, time

try:
    import Quartz
    from AppKit import NSRunningApplication, NSWorkspace, NSScreen, NSWindow, \
                       NSApplicationActivateIgnoringOtherApps
except ImportError:
    sys.exit("pyobjc not found. Bring it in with NIX (never brew):\n"
             '  nix-shell -p "python3.withPackages (ps: [ps.pyobjc-framework-Quartz'
             ' ps.pyobjc-framework-Cocoa])" --run "python3 tools/perf/bench_fps_macos.py"')

# Standard cross-machine ladder. The actual sweep is derived per display in main():
# ladder points this display can realize, topped by the display-max point.
LADDER = [(640, 360), (960, 540), (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160)]
WINDOW_FRAMES = 128  # engine ANO_PERF_WINDOW_FRAMES; frames per [frame]/[frametime]/[profile] window
WARMUP_S = 2.0       # leading seconds of [frame]/[frametime] windows to discard
KDM_NATIVE = 0x02000000  # IOKit kDisplayModeNativeFlag: the panel's own pixel grid

# Engine env applied to every run before --env; --env wins per key. The bench measures the
# shadow-culled path by default -- pass --env ANO_SHADOW_BUDGET=0 for the uncapped baseline.
# ANO_FLOAT keeps the window above normal windows (unoccluded -> FRONT without stealing focus,
# which macOS 14 cooperative activation denies to background scripts). ANO_POS is computed in
# main() from the measured display, never hardcoded. House rule: every mac run carries the
# Metal performance HUD (MTL_HUD_*), part of the standard config and echoed in ENV_VARS.
ENGINE_DEFAULTS = {"ANO_SHADOW_BUDGET": "2", "ANO_FLOAT": "1",
                   "MTL_HUD_ENABLED": "1", "MTL_HUD_VISIBLE": "1"}

# Engine log contract, same regexes as the win64/linux drivers.
PF = re.compile(r"\[frame\] ([0-9.]+) fps")
PT = re.compile(r"\[frametime\].*?p50=([0-9.]+) p90=([0-9.]+) p99=([0-9.]+) p999=([0-9.]+) max=([0-9.]+)")
PG = re.compile(r"total=([0-9.]+)")
PS = re.compile(r"swap=([0-9.]+)")
PR = re.compile(r"frusta ([0-9.]+)")
PX = re.compile(r"res=(\d+)x(\d+)")


def _display_info():
    """Main-display facts, measured not assumed. Three resolutions coexist on a scaled Retina
    desktop: the physical panel, the desktop mode in points, and the backing store (points x
    backingScaleFactor) that windows -- and the swapchain -- actually render into. The backing
    store can exceed the panel (1440x900 pt @2x = 2880x1800 px onto a 2560x1600 panel);
    WindowServer downsamples the result. Bench pixels are backing pixels."""
    s = NSScreen.mainScreen()
    scale = float(s.backingScaleFactor()) if s else 1.0
    fr, vf = s.frame(), s.visibleFrame()
    # Physical panel: the native-flagged display mode; largest-pixel mode as the fallback.
    did = Quartz.CGMainDisplayID()
    dims = [(Quartz.CGDisplayModeGetPixelWidth(m), Quartz.CGDisplayModeGetPixelHeight(m),
             Quartz.CGDisplayModeGetIOFlags(m))
            for m in (Quartz.CGDisplayCopyAllDisplayModes(did, None) or [])]
    native = [(w, h) for (w, h, fl) in dims if fl & KDM_NATIVE]
    panel = native[0] if native else (max((w, h) for (w, h, _) in dims) if dims else None)
    # Exact titled-frame overhead via class-method geometry; no window is created.
    try:
        r = NSWindow.frameRectForContentRect_styleMask_(((0, 0), (256, 256)), 1)  # titled mask
        titlebar = float(r.size.height) - 256.0
    except Exception:
        titlebar = 28.0
    # visibleFrame excludes the menu bar and dock. Cocoa origin is bottom-left; GLFW's top-left.
    inset = (int(vf.origin.x), int(fr.size.height - (vf.origin.y + vf.size.height)))
    maxc_pt = (int(vf.size.width), int(vf.size.height - titlebar))  # largest titled-window content
    return {"scale": scale, "panel_px": panel,
            "mode_pt": (int(fr.size.width), int(fr.size.height)),
            "backing_px": (int(fr.size.width * scale), int(fr.size.height * scale)),
            "titlebar_pt": titlebar, "inset_pt": inset,
            "maxc_pt": maxc_pt,
            "maxc_px": (int(maxc_pt[0] * scale), int(maxc_pt[1] * scale))}


def _find_window(pid):
    """Bounds dict of the engine's on-screen window for pid via CGWindowList, None until mapped."""
    wins = Quartz.CGWindowListCopyWindowInfo(
        Quartz.kCGWindowListOptionOnScreenOnly | Quartz.kCGWindowListExcludeDesktopElements,
        Quartz.kCGNullWindowID) or []
    for w in wins:
        if w.get("kCGWindowOwnerPID") == pid and w.get("kCGWindowBounds", {}).get("Height", 0) > 40:
            return w["kCGWindowBounds"]
    return None


def _activate(pid):
    """Best-effort activation. macOS 14 cooperative activation declines focus grabs from
    background scripts (returns True, does nothing), so this is never the verification."""
    app = NSRunningApplication.runningApplicationWithProcessIdentifier_(pid)
    if app:
        app.activateWithOptions_(NSApplicationActivateIgnoringOtherApps)
    time.sleep(0.3)


def _front_state(pid):
    """FRONT on macOS: the app is active, or its window shows <5% occlusion by normal-layer
    windows above it (CGWindowList is front-to-back). Measured on this stack, even full
    occlusion moves throughput <1%, but the gate stays strict; ANO_FLOAT makes it hold
    structurally by floating the window above every normal window."""
    front = NSWorkspace.sharedWorkspace().frontmostApplication()
    if front and front.processIdentifier() == pid:
        return True
    wins = Quartz.CGWindowListCopyWindowInfo(
        Quartz.kCGWindowListOptionOnScreenOnly | Quartz.kCGWindowListExcludeDesktopElements,
        Quartz.kCGNullWindowID) or []
    idx, t = None, None
    for i, w in enumerate(wins):
        b = w.get("kCGWindowBounds", {})
        if w.get("kCGWindowOwnerPID") == pid and b.get("Height", 0) > 40:
            idx, t = i, b
            break
    if t is None:
        return False
    covered = 0.0
    for w in wins[:idx]:                         # earlier in the list = in front of the target
        if w.get("kCGWindowLayer", 0) != 0: continue
        b = w.get("kCGWindowBounds", {})
        ox = max(0.0, min(t["X"] + t["Width"],  b["X"] + b["Width"])  - max(t["X"], b["X"]))
        oy = max(0.0, min(t["Y"] + t["Height"], b["Y"] + b["Height"]) - max(t["Y"], b["Y"]))
        covered += ox * oy                       # summed overlaps overstate: errs strict
    return covered < 0.05 * t["Width"] * t["Height"]


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


def run_once(exe, w, h, dur, menu, env, scale):
    # Logging refactor (b85e213): each run writes logs/<session-stamp>_ano.log, no fixed anoptic.log.
    # Snapshot preexisting logs, then pick up whichever new file this process opens.
    logdir = os.path.join(os.path.dirname(exe), "logs")
    def _logfiles():
        try: return {os.path.join(logdir, n) for n in os.listdir(logdir) if n.endswith("_ano.log")}
        except FileNotFoundError: return set()
    pre = _logfiles()

    # Framebuffer target -> window points for ANO_RES. Fractional points warn, swap= arbitrates.
    pw, ph = round(w / scale), round(h / scale)
    if (pw * scale, ph * scale) != (w, h):
        print(f"WARNING: {w}x{h} px is not integral at scale {scale:g}; "
              f"requesting {pw}x{ph} pts, the render column arbitrates", file=sys.stderr)
    env = dict(env)
    env["ANO_RES"] = f"{pw}x{ph}"
    if menu: env["ANO_MENU"] = "1"
    else:    env.pop("ANO_MENU", None)

    p = subprocess.Popen([exe], env=env)
    t0 = time.perf_counter()
    bounds = None
    while time.perf_counter() - t0 < 15 and not bounds:
        bounds = _find_window(p.pid); time.sleep(0.1)

    front = False
    if bounds:
        _activate(p.pid)
        front = _front_state(p.pid)
        # Frame is content + title bar in points; a short frame means AppKit clamped it to the display.
        if bounds["Width"] + 2 < pw or bounds["Height"] + 2 < ph:
            print(f"WARNING: requested {pw}x{ph} pts, window frame is "
                  f"{bounds['Width']:.0f}x{bounds['Height']:.0f} pts (display too small?), the render column has the truth",
                  file=sys.stderr)

    buf, part, f = [], "", None
    log = None
    while time.perf_counter() - t0 < dur:
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
    ap = argparse.ArgumentParser(description="anopticengine wall-clock FPS / GPU-pass bench -- MACOS driver.")
    ap.add_argument("--exe", default="build/Release/anopticengine")
    ap.add_argument("--res", help="single WxH framebuffer pixels, e.g. 1920x1080 (default: resolution sweep)")
    ap.add_argument("--dur", type=float, default=30.0,
                    help="seconds per data point (default: exactly 30)")
    ap.add_argument("--no-menu", action="store_true", help="static HUD only (no menu open)")
    ap.add_argument("--churn", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--env", action="append", default=[],
                    help="KEY=VAL engine env var (repeatable); overrides defaults. "
                         "Default caps ANO_SHADOW_BUDGET=2; pass =0 for the uncapped path.")
    args = ap.parse_args()

    if args.churn:
        sys.exit("--churn is unsupported on macOS: storming a live window's size needs Accessibility")
    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        sys.exit(f"exe not found: {exe} (build it, e.g. ./build.sh 1)")

    disp = _display_info()
    scale = disp["scale"]

    env = dict(os.environ)
    env.update(ENGINE_DEFAULTS)                  # harness defaults over ambient
    # Placement from the measured display, not a magic constant: content lands below the menu
    # bar plus title bar, so AppKit never constrains the frame and the display-max point fits.
    env["ANO_POS"] = f"{disp['inset_pt'][0]}x{int(disp['inset_pt'][1] + disp['titlebar_pt'])}"
    for kv in args.env:                          # --env wins over defaults
        k, _, v = kv.partition("="); env[k] = v
    if "ANO_RES" in env: sys.exit("ANO_RES is the driver's sizing mechanism: use --res, not --env ANO_RES")
    if "ANO_MENU" in env: sys.exit("ANO_MENU is the driver's menu mechanism: use --no-menu, not --env ANO_MENU")
    # MTL_* included: the Metal HUD is part of the standard mac config and belongs in the writeup.
    ano = {k: env[k] for k in env if k.startswith(("ANO_", "MTL_"))}
    print("ENV_VARS: " + ", ".join(f"{k}={ano[k]}" for k in sorted(ano)))  # paste into the bench template

    # The retina story, stated instead of assumed. Targets below are framebuffer (backing) pixels.
    pw, ph = disp["panel_px"] or (0, 0)
    mw, mh = disp["mode_pt"]; bw, bh = disp["backing_px"]
    over = " -- scaled mode: renders past the panel, WindowServer downsamples" \
           if (pw and (bw > pw or bh > ph)) else ""
    print(f"display: panel {pw}x{ph} px native; desktop mode {mw}x{mh} pt @ {scale:g}x "
          f"= {bw}x{bh} px backing{over}")
    cw, ch = disp["maxc_pt"]; xw, xh = disp["maxc_px"]
    print(f"window max: {cw}x{ch} pt content (visible frame minus {disp['titlebar_pt']:g} pt "
          f"title bar) = {xw}x{xh} px, the largest framebuffer a window here can realize")

    if args.res:
        w, h = (int(x) for x in args.res.lower().split("x")); sizes = [(w, h)]
        if w > xw or h > xh:
            print(f"WARNING: {w}x{h} exceeds this display's {xw}x{xh} max; AppKit will clamp, "
                  f"the render column has the truth", file=sys.stderr)
    else:
        sizes = [p for p in LADDER if p[0] <= xw and p[1] <= xh]
        dropped = [p for p in LADDER if p not in sizes]
        if (xw, xh) not in sizes:
            sizes.append((xw, xh))               # display-max point: the full-desktop datum
        if dropped:
            print("sweep: dropped " + ", ".join(f"{w}x{h}" for w, h in dropped)
                  + f" (exceed this display); display max {xw}x{xh} tops the sweep")

    print(f"{'target':>11} {'front':>5} {'render':>11} {'swapMiB':>8} {'avgFPS':>7} {'p50':>7} "
          f"{'1%low':>7} {'0.1%low':>7} {'maxms':>7} {'GPUms':>7} {'GPUcap':>7} {'w/cap':>6} "
          f"{'frusta':>6}  bound")
    short = []
    for (w, h) in sizes:
        r = run_once(exe, w, h, args.dur, not args.no_menu, env, scale)
        rr = f"{r['res'][0]}x{r['res'][1]}" if r["res"] else "?"
        print(f"{f'{w}x{h}':>11} {'FRONT' if r['front'] else 'BG!!':>5} {rr:>11} {r['swap']:8.1f} "
              f"{r['avg_fps']:7.1f} {r['p50']:7.1f} {r['low1']:7.1f} {r['low01']:7.1f} "
              f"{r['ft_max']:7.3f} {r['gpu_ms']:7.3f} {r['gpu_cap']:7.0f} {r['ratio']:6.2f} "
              f"{r['frusta']:6.1f}  {r['bound']}")
        if r["bound"] == "?":
            short.append(f"{w}x{h}")
    # A run too short for GPU profile windows never passes silently.
    if short:
        sys.exit(f"ERROR: no GPU profile window survived warmup at {', '.join(short)} -- "
                 f"the run is too short for that point's fps; rerun with a longer --dur")


if __name__ == "__main__":
    main()
