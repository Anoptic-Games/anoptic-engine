# YYYY-MM-DD FPS benchmark

<!--
- Specs come from CIM/registry CPU: Get-CimInstance Win32_Processor. GPU name+driver: Win32_VideoController. VRAM: HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000 HardwareInformation.qwMemorySize (Win32_VideoController.AdapterRAM caps at 4 GB, ignore it). RAM: Win32_PhysicalMemory. Board/BIOS: Win32_BaseBoard, Win32_BIOS. OS build: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion (DisplayVersion + UBR). Scale: HKCU\Control Panel\Desktop\WindowMetrics AppliedDPI / 96.
- NVIDIA marketing driver = last 5 digits of the Windows version, e.g. 32.0.15.7261 -> 572.61.
- Numbers: paste the harness table verbatim; it already drops warmup (first 2 s by elapsed time — window cadence scales with fps) and takes per-point medians, including the frametime lows. Discard any BG!! row (a background window mismeasures the GPU passes).
- The res column is the harness render column: the realized swapchain extent from the engine's res= profile line. Rows are named by it. The sweep is display-derived, so the top row is the display max and its label varies per machine.
- Resolution check is mandatory: swap MiB must scale ~linearly with render pixel count (near-constant MiB per megapixel). swap cross-checks res; a render/swap disagreement means an engine accounting bug.
- Carry the harness display line (panel, mode, scale, largest realizable framebuffer) into System/Mode.
-->

## System

- CPU: <model>, <cores>C/<threads>T (<arch>), <socket>, <base GHz> base
- GPU: <model>, <VRAM> GB, driver <marketing ver> (Windows <x.x.x.x>, <date>)
- RAM: <total> GB (<n>x<size> GB) <part number>, <type>-<speed> at <MT/s> MT/s
- Motherboard: <vendor> <model>, BIOS <ver>
- OS: <edition> <feature ver>, build <build.ubr>, x64
- Display: <WxH> at <Hz> Hz, <scale>% scale

## Runtime environment

- Engine: Anoptic at commit <sha> (branch <branch>)
- Build: <config> -O<n>, CMake + Ninja, <compiler> <ver> (<toolchain>, target <triple>)
- Renderer: Vulkan, <mesh shaders on/off>, MSAA <N>x, present mode <VK_PRESENT_MODE_*> (<vsync state>), render loop <throttled state>
- Scene: <scene>, lighting mode <MODE>, <N of 42> shadow frusta per frame, <HUD menu open/closed>
- Harness: tools/perf/bench_fps_<platform>.py, <flags or "default sweep">, <dur> s per point, warmup dropped, per-point medians, foreground-verified

ENV_VARS: [[enumerate]]

## Window manager

<compositor or window manager: Desktop Window Manager (DWM) on Windows; Mutter/KWin/Hyprland/Xwayland on Linux; WindowServer on macOS>. <composited, or exclusive fullscreen>.

## Mode

<windowing mode: borderless windowed (WS_POPUP), windowed, or exclusive fullscreen>, <how the client area is sized>, <DPI awareness>. <which rows fill the native desktop>.

## Results

| res | swap MiB | wall fps | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| <render WxH> | <swap> | <fps> | <fps> | <fps> | <ms> | <ms> | <cap> | <ratio> | <frusta> | <GPU or CPU/present> |

For textbuffer viewing convenience:
```
┌──────────────┬──────────┬──────────┬────────┬──────────┬────────┬────────┬─────────┬──────────┬──────────┬──────────────────────┐
│     res      │ swap MiB │ wall fps │ 1% low │ 0.1% low │ max ms │ GPU ms │ GPU cap │ wall/cap │  frusta  │        bound         │
├──────────────┼──────────┼──────────┼────────┼──────────┼────────┼────────┼─────────┼──────────┼──────────┼──────────────────────┤
│ <render WxH> │ <swap>   │ <fps>    │ <fps>  │ <fps>    │ <ms>   │ <ms>   │ <cap>   │ <ratio>  │ <frusta> │ <GPU or CPU/present> │
└──────────────┴──────────┴──────────┴────────┴──────────┴────────┴────────┴─────────┴──────────┴──────────┴──────────────────────┘
```

All rows foreground-verified; drop any BG!! row. res is the harness render column — the realized swapchain extent from the engine's res= profile line (note the target beside it if the two differ). wall fps is the median per-window throughput from the [frame] line (ANO_PERF_WINDOW_FRAMES = 128 presented frames per window). 1% low and 0.1% low are 1000/p99 and 1000/p999 from the [frametime] line, each percentile the median across windows; max ms is the run's worst single frame. At n=128 per window p999 saturates toward the window max, so the 0.1% low reads as the typical worst-frame-per-128; max ms still catches the absolute spike. GPU ms is the median GPU-pass total (upload + compute + shadow + lighting + composite); GPU cap = 1000 / GPU ms; wall/cap at or above 0.9 is GPU-bound, below is CPU/present-bound; swap MiB is the swapchain allocator's resident VRAM.

Resolution check: <swap MiB per megapixel of render pixels across the sweep; the top-row-to-1080p swap ratio against the same rows' pixel ratio>. The render column is the extent the engine actually created; near-linear swap-vs-render-pixels rules out an accounting bug on top of it.
