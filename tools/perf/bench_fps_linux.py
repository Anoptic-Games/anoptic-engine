#!/usr/bin/env python3
"""anopticengine FPS / GPU-pass benchmark harness -- LINUX (X11/Xwayland) DRIVER.

Sibling of tools/perf/bench_fps_win64.py. The measurement METHODOLOGY -- the engine
log contract it parses, the foreground/DPI/warmup rules, and how to read the numbers --
is platform-agnostic and lives in tools/perf/README.md. This file re-implements that SAME
contract with Linux window primitives; the Windows and macOS drivers do the same.

What is Linux-specific here (and differs from the Win64 driver):
  - window discovery by PID via xdotool search --pid (GLFW sets _NET_WM_PID), name fallback
  - forced + VERIFIED foreground via xdotool windowactivate --sync, confirmed against
    xdotool getactivewindow (a background/occluded window mismeasures the GPU passes)
  - borderless exact-size render surface via _MOTIF_WM_HINTS strip + xdotool windowsize --sync
  - 'M' menu toggle synthesized with XTEST (xdotool key) to the focused window

What is shared (engine side, same on every target) -- lines in anoptic.log:
  [frame] <fps> fps <ms> ms wall            -- wall-clock throughput (profiling.c: ano_frame_mark)
  [profile mode=...] total=<ms> (frusta N/42) ... swap=<MiB>   -- GPU-pass profile + VRAM

Note on physical pixels: unlike Windows (logical/scaled desktop coords), X11 hands out device
pixels, and this driver commands exact pixel window sizes -- so there is no DPI mislabel to
defeat. The swap= VRAM sanity check in tools/perf/README.md remains the authority for "what
resolution did I actually render", especially when comparing two machines.

Tools required (bring them in with NIX, never apt): xdotool (required), wmctrl + xprop
(optional, improve activation/borderless). e.g.:
  nix shell nixpkgs#xdotool nixpkgs#wmctrl nixpkgs#xorg.xprop nixpkgs#xorg.xrandr
Wayland: this drives X11/Xwayland clients. A GLFW window on the native Wayland backend is
not controllable here -- run it as an X11/Xwayland client (both DISPLAY and an X server present).

Requires: Linux, Python 3, an X server (Xorg or Xwayland). Dev-only tool; not built or shipped.

Examples:
  python3 tools/perf/bench_fps_linux.py                          # resolution sweep, menu open
  python3 tools/perf/bench_fps_linux.py --res 3840x2160 --dur 30
  python3 tools/perf/bench_fps_linux.py --no-menu                # static HUD only
  python3 tools/perf/bench_fps_linux.py --churn                  # resize-storm stress (one row)
  python3 tools/perf/bench_fps_linux.py --env ANO_SHADOW_BUDGET=2
"""
import argparse, os, re, shutil, subprocess, sys, time

SWEEP_DEFAULT = [(640, 360), (960, 540), (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160)]
CHURN_SIZES   = [(640, 480), (1920, 1080), (900, 1500), (2560, 1440), (480, 900),
                 (1600, 900), (1280, 720), (2200, 1300), (720, 1280), (1100, 1900)]
CHURN_MS = 33.0  # target floor; xdotool spawn overhead makes the real cadence a little slower
PRINT_INTERVAL = 120  # engine ANO_PROFILE_PRINT_INTERVAL; 120 rendered frames per profile line

# Engine log contract -- identical regexes to the Win64 driver (parse the same two lines).
PF = re.compile(r"\[frame\] ([0-9.]+) fps")
PG = re.compile(r"total=([0-9.]+)")
PS = re.compile(r"swap=([0-9.]+)")
PR = re.compile(r"frusta ([0-9.]+)")


def _run(cmd):
    """Run a helper tool, return stripped stdout ('' on any failure). Never raises."""
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=5).stdout.strip()
    except Exception:
        return ""


def _active_window():
    return _run(["xdotool", "getactivewindow"])


def _find_window(pid):
    """Window id for pid: prefer _NET_WM_PID (xdotool --pid), fall back to the GLFW title 'Vulkan'
    cross-checked against the child pid. Decimal ids throughout (same space as getactivewindow)."""
    ids = [w for w in _run(["xdotool", "search", "--onlyvisible", "--pid", str(pid)]).split() if w]
    if not ids:
        for w in _run(["xdotool", "search", "--name", "Vulkan"]).split():
            if _run(["xdotool", "getwindowpid", w]) == str(pid):
                ids = [w]
                break
    return ids[0] if ids else None


def _strip_decorations(wid):
    # Motif hint: flags=MWM_HINTS_DECORATIONS(2), decorations=0 -> borderless, so windowsize is the render size.
    if shutil.which("xprop"):
        _run(["xprop", "-id", wid, "-f", "_MOTIF_WM_HINTS", "32c",
              "-set", "_MOTIF_WM_HINTS", "2, 0, 0, 0, 0"])


def _resize(wid, w, h, sync):
    cmd = ["xdotool", "windowsize"] + (["--sync"] if sync else []) + [wid, str(w), str(h)]
    _run(cmd)
    _run(["xdotool", "windowmove", wid, "0", "0"])


def _bring_to_front(wid):
    """Force the window to the true foreground and CONFIRM it. A background/occluded window
    mismeasures the GPU passes -- never trust a row that isn't front."""
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
    # Focus, then XTEST 'm' to the focused window (real event; GLFW ignores XSendEvent synthetics).
    _run(["xdotool", "windowfocus", "--sync", wid])
    if _run(["xdotool", "getwindowfocus"]) != wid:
        return  # focus refused: skip rather than type 'm' into whatever IS focused
    _run(["xdotool", "key", "--clearmodifiers", "m"])


def _median(a):
    s = sorted(a)
    return s[len(s) // 2] if s else 0.0


def parse_stream(lines):
    """Fold engine log lines into (fps, total_ms, swap_MiB, frusta) sample lists. Shared by the
    live tail below and by offline replay of a captured anoptic.log."""
    fps, tot, sw, fru = [], [], [], []
    for line in lines:
        m = PF.search(line);  m and fps.append(float(m.group(1)))
        if "profile mode=" in line:
            g = PG.search(line); g and tot.append(float(g.group(1)))
            s = PS.search(line); s and sw.append(float(s.group(1)))
            r = PR.search(line); r and fru.append(float(r.group(1)))
    return fps, tot, sw, fru


def summarize(fps, tot, sw, fru, front=True):
    """Drop warmup, take medians, derive GPUcap and the wall/cap bound indicator (tools/perf/README.md)."""
    fps, tot, fru = fps[2:], tot[4:], fru[4:]
    wf, gt = _median(fps), _median(tot)
    cap = 1000.0 / gt if gt else 0.0
    ratio = wf / cap if cap else 0.0
    return {"front": front, "swap": (sw[-1] if sw else 0.0), "wall_fps": wf,
            "gpu_ms": gt, "gpu_cap": cap, "ratio": ratio, "frusta": _median(fru),
            "bound": "GPU" if ratio > 0.9 else "CPU/present"}


def run_once(exe, w, h, dur, menu, churn, env):
    log = os.path.join(os.path.dirname(exe), "anoptic.log")
    try: os.remove(log)                          # no file lock on Linux; a plain unlink suffices
    except FileNotFoundError: pass

    p = subprocess.Popen([exe], env=env)
    t0 = time.perf_counter()
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
    ap.add_argument("--dur", type=float, default=15.0, help="seconds per data point")
    ap.add_argument("--no-menu", action="store_true", help="static HUD only (no menu open)")
    ap.add_argument("--churn", action="store_true", help="resize-storm stress; single row")
    ap.add_argument("--env", action="append", default=[], help="KEY=VAL engine env var (repeatable)")
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
    # GLFW 3.4 prefers native Wayland when WAYLAND_DISPLAY is set; hide it from the child so the
    # engine comes up as an Xwayland client xdotool can drive (a --env override can restore it).
    env.pop("WAYLAND_DISPLAY", None)
    for kv in args.env:
        k, _, v = kv.partition("="); env[k] = v
    if args.env: print("engine env: " + "  ".join(args.env))

    if args.churn:
        sizes = [(3840, 2160)]  # base res; the run cycles CHURN_SIZES internally
    elif args.res:
        w, h = (int(x) for x in args.res.lower().split("x")); sizes = [(w, h)]
    else:
        sizes = SWEEP_DEFAULT

    print(f"{'res':>11} {'front':>5} {'swapMiB':>8} {'wallFPS':>8} {'GPUms':>7} "
          f"{'GPUcap':>7} {'wall/cap':>8} {'frusta':>7}  bound")
    for (w, h) in sizes:
        r = run_once(exe, w, h, args.dur, not args.no_menu, args.churn, env)
        label = "churn" if args.churn else f"{w}x{h}"
        print(f"{label:>11} {'FRONT' if r['front'] else 'BG!!':>5} {r['swap']:8.1f} "
              f"{r['wall_fps']:8.1f} {r['gpu_ms']:7.3f} {r['gpu_cap']:7.0f} "
              f"{r['ratio']:8.2f} {r['frusta']:7.1f}  {r['bound']}")


if __name__ == "__main__":
    main()
