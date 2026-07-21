---
name: verify
description: Drive an anoptic-engine change at its runtime surface 〜 nix run to build+launch, screenshot-macos to capture, eval+substitution for Linux artifacts from a Mac.
---

## macOS surface (this machine)

`nix run . [-- N]` is the impure entry: dev-shell `./build.sh N` (default 1), then launches `build/<Release|Debug>/anopticengine` for N=1|2 and `build/Headless/anopticengine` for N=3 (console, no GPU). Every run is a full rebuild (`ano_scrub` deletes all objects) 〜 Release takes minutes. Run it backgrounded, watch the log for `[anoptic] launching`, then capture the window with the screenshot-macos skill (owner `anoptic`) and `kill <pid>`. SIGTERM exits 143 through the exec chain; invalid mode exits 1 with build.sh usage; profiles 4-8 run ctest and never launch.

## Linux artifacts from this Mac

No Linux builder or container runtime here. Verify to the platform boundary: `nix eval .#packages.x86_64-linux.<variant>.postFixup --raw` shows the generated fixup script with real store paths; `nix-store --realise <path>` substitutes those paths from cache.nixos.org so their contents can be inspected; `nix build --dry-run .#packages.x86_64-linux.<variant>` proves the derivation instantiates. Actual execution needs a Linux host.

## Gotchas

ano_sleep on Darwin is a mach_wait_until deadline hybrid (src/time/time_macos.c) 〜 lands within ~41ns of the request in any human-run band. The macOS nix-daemon sandbox coalesces kernel timers beyond what any yielding sleep can meet, so anotest_time asserts sleep ceilings only outside it (gate: NIX_BUILD_TOP set and IN_NIX_SHELL unset skips ceilings, floors always assert). `nix develop` sets both vars, so `nix run` test profiles keep full assertions. Headless launch profile is `nix run -- 3` → `build/Headless/anopticengine`, console loop logging "Waiting..." every 3s.
