<!--
Transcript: Claude Code session verifying that tools/perf FPS numbers are real uncapped throughput, not a measurement bug. Keep the chain of evidence intact.
-->

 ▐▛███▜▌   Claude Code v2.1.207
▝▜█████▛▘  Opus 4.8 · Claude Max
  ▘▘ ▝▝    ~\Code\anoptic-engine

 ▎ Extended: Fable 5 is included in your weekly limit
 ▎ Through July 12, you can use up to 50% of your weekly usage limit on Fable 5. If you hit your limit, you can continue on Fable 5 with usage credits. Fable 5 draws down usage faster than Opus 4.8. Run /model and select Fable to use it. Learn more (https://support.claude.com/en/articles/15424964-claude-fable-5-promotional-access)
   +1 more · /status

❯ Verify that the benchmarks in tools/perf are actually recording the FPS       
  accurately. The numbers I'm getting are rather high.                          

● tools/perf drivers, ano_frame_mark(), drawFrame present gate, IMMEDIATE present mode, unthrottled render loop, TSC/QPC calibration, per-frame fence wait.

● Verdict: FPS numbers are accurate. High values by design, not a measurement bug.

  Chain of evidence, log line → clock:

  1. Drivers report the engine's number verbatim. bench_fps_win64.py and bench_fps_linux.py regex [frame] ([0-9.]+) fps out of anoptic.log and take the median. No scaling, no derivation.
  2. Engine arithmetic is correct. ano_frame_mark() (profiling.c:133): fps = frames·1e6 / dt_us. Seed frame uncounted, window resets on each print, no off-by-one. ms wall is the reciprocal (fps·ms ≈ 1000).
  3. Counted once per actually presented frame. Single call site, vulkanMaster.c:288, after vkQueuePresentKHR succeeds. Early returns before it skip the mark: out-of-date acquire (:195), acquire failure (:199), submit failure (:249), present suboptimal/out-of-date (:270), present failure (:274). Swapchain recreations and dropped frames do not inflate; not per-swapchain-image.
  4. Count tracks real GPU throughput, not CPU spinning ahead. vkWaitForFences at the top of drawFrame (:172) caps CPU at MAX_FRAMES_IN_FLIGHT ahead of the GPU; vkAcquireNextImageKHR blocks when images are in flight. Over ~700 frames/sec, a few in-flight frames is noise.
  5. Clock is trustworthy. ano_timestamp_us() uses TSC calibrated against QPC, median-of-3, sanity-banded 100 MHz-100 GHz (time_win64.c:104-133). tests/anotest_time.c cross-checks that waits land near real durations.

  High numbers:
  - Present mode VK_PRESENT_MODE_IMMEDIATE_KHR (vulkanMaster.c:314), no vsync.
  - Render loop (main.c:638-642) is glfwPollEvents(); drawFrame(); no sleep or frame limiter.
  Documented 680-814 fps on a 4090 is genuine throughput.

  Nuance: metric counts frames completed and submitted to present. Under IMMEDIATE with tearing, many never fully scan out to the monitor. "Engine frame throughput," not "frames the display showed." bench_fps.md:9 intends whole-pipeline throughput. FIFO/vsync would pin to refresh rate; that changes the quantity measured, not a fix for inaccuracy.

  Nit: _median() in both drivers returns the upper-middle element for even-length sample lists instead of averaging the two middle ones. Negligible high-bias over ~11 samples.

✻ Cogitated for 3m 38s

※ recap: Verified whether tools/perf FPS benchmarks record accurately: they do. The high numbers are real uncapped throughput, since present mode is IMMEDIATE and the render loop is unthrottled, not a measurement bug. Next: optionally switch to FIFO/vsync or fix the minor median bias if you want. (disable recaps in /config)
