{
  # Pure side — artifacts in ./result, every dep pinned, working-tree state irrelevant:
  #   nix build                            renderer, Release+ThinLTO, this platform
  #   nix build .#debug                    renderer, Debug, validation layers wired in
  #   nix build .#release-headless         no-renderer engine (alias: .#headless)
  #   nix build .#<type>[-headless]-<platform>-<arch>[-<backend>]   any permutation:
  #     release-linux-x64[-wayland|-x11]   debug-linux-x64[-wayland|-x11]
  #     release-headless-linux-x64         debug-headless-linux-x64
  #     release-linux-x64-anygpu (alias: .#anygpu)   debug-linux-x64-anygpu   bundled mesa ICDs
  #   nix run .#nvidia|.#nvidia-debug      pure engine on the host NVIDIA driver (nixglhost)
  #     (same set with -aarch64 on ARM Linux; -macos-aarch64 on Apple Silicon)
  #     release-windows-x64 (alias: release-wsl)  debug-windows-x64  *-headless-windows-x64
  #   nix build .#tests-headless           run a CTest suite in the sandbox (fails = red)
  #   nix build .#tests-asan|tests-tsan    sanitized non-GPU suite        (Linux)
  #   nix build .#tests-full               full suite incl. Vulkan on lavapipe (Linux, experimental)
  #   nix flake check                      all of the host's suites at once
  #
  # Impure side — your working tree, output in ./build/<label>/ like build.sh:
  #   nix run [-- N]                       dev-env wrapper around ./build.sh N (default 1):
  #                                        halts if submodule gitlinks disagree with the pins,
  #                                        auto-inits absent submodules, stages assets/ best-effort,
  #                                        then launches the engine for N=1|2 (renderer) or N=3
  #                                        (headless console, runs in WSL too); test profiles run ctest
  #                                        foreign distros bridge to the host GPU (nixglhost / mesa ICDs)
  #   nix develop [.#windows]              the same env, you drive
  #
  # git flakes see tracked files only: `git add` new files or nix will not.
  description = "Anoptic Engine — C23 game engine (Linux, macOS, Windows via MinGW cross)";

  inputs = {
    # Same rev as the pylon system flake.
    nixpkgs.url = "github:NixOS/nixpkgs/b5aa0fbd538984f6e3d201be0005b4463d8b09f8";

    # Pinned submodule sources, revs match .gitmodules.
    mimalloc-src = {
      url = "github:microsoft/mimalloc/02a2f5df9d7d46d30263b83832eebeeab62dc5fe";
      flake = false;
    };
    freetype-src = {
      url = "github:freetype/freetype/b1f47850878d232eea372ab167e760ccac4c4e32";
      flake = false;
    };
    glfw-src = {
      url = "github:glfw/glfw/7b6aead9fb88b3623e3b3725ebb42670cbe4c579";
      flake = false;
    };
    cgltf-src = {
      url = "github:jkuhlmann/cgltf/85cd62382dfea638278962690cf515023f33ed00";
      flake = false;
    };

    # Public asset pack. Private full pack:
    #   nix build --override-input anoptic-assets git+ssh://git@github.com/Anoptic-Games/assets
    anoptic-assets = {
      url = "github:Anoptic-Games/assets-free";
      flake = false;
    };

    # Host NVIDIA userspace bridge for non-NixOS hosts.
    nix-gl-host = {
      url = "github:numtide/nix-gl-host";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      mimalloc-src,
      freetype-src,
      glfw-src,
      cgltf-src,
      anoptic-assets,
      nix-gl-host,
    }:
    let
      lib = nixpkgs.lib;
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = f: lib.genAttrs systems f;

      # Feed each attr its own name in as the variant.
      mkVariants = builder: lib.mapAttrs (variant: args: builder (args // { inherit variant; }));

      # vulkan-validation-layers where the platform has them.
      vvlFor =
        pkgs: host:
        if pkgs ? vulkan-validation-layers && lib.meta.availableOn host pkgs.vulkan-validation-layers then
          pkgs.vulkan-validation-layers
        else
          null;

      # Store mesa Vulkan ICDs, colon-joined: hardware first, lvp last.
      mesaVkIcdPaths =
        pkgs: host:
        lib.concatStringsSep ":" (
          map (d: "${pkgs.mesa}/share/vulkan/icd.d/${d}_icd.${host.parsed.cpu.name}.json") [
            "radeon"
            "intel"
            "nouveau"
            "lvp"
          ]
        );

      # libdecor without its GTK plugin: the cairo plugin draws the decorations and GTK3's
      # ~290 MiB closure stays out of the engine's runtime.
      libdecorSlim =
        pkgs:
        pkgs.libdecor.overrideAttrs (o: {
          mesonFlags = o.mesonFlags ++ [ (lib.mesonEnable "gtk" false) ];
        });

      # Fortify off for Debug and sanitizer builds (needs -O).
      fortifyOff = [
        "fortify"
        "fortify3"
      ];

      # Copy a pinned submodule source into the unpacked tree.
      injectSubmodule = name: src: ''
        rm -rf "$sourceRoot/external/${name}"
        cp -r ${src} "$sourceRoot/external/${name}"
        chmod -R u+w "$sourceRoot/external/${name}"
      '';

      # path=rev pairs shared by the shell warning and the nix-run fatal gate.
      pinList = "external/glfw=${glfw-src.rev} external/mimalloc=${mimalloc-src.rev} external/freetype=${freetype-src.rev} external/cgltf=${cgltf-src.rev}";

      # Shell-entry warning when recorded gitlinks disagree with the flake pins.
      submodulePinWarn = ''
        if git rev-parse --git-dir >/dev/null 2>&1; then
          for pair in ${pinList}; do
            p="''${pair%%=*}" want="''${pair#*=}"
            rec="$(git ls-tree HEAD "$p" 2>/dev/null | awk '{ print $3 }')"
            if [ -n "$rec" ] && [ "$rec" != "$want" ]; then
              echo "[anoptic] WARNING: $p gitlink $rec != flake pin $want — stale submodule commit. Run 'git submodule update --init' and commit the corrected pointer." >&2
            fi
          done
        fi
      '';

      # One engine derivation for every permutation.
      # pkgs/stdenv: host package set + compiler (native clang+lld, or MinGW cross).
      # variant: qualified attr name, becomes the pname suffix.
      # buildType: Release | Debug. headless: no renderer, no GLFW/Vulkan.
      # wayland/x11: Linux renderer backends (both on = runtime-selected).
      # tests: ANOPTIC_TESTS + ctest in checkPhase. sanitize: asan | tsan | "".
      # softwareVulkan: point the loader at mesa's lavapipe ICD (sandboxed GPU tests).
      # anyGpu: mesa ICDs as a VK_ADD_DRIVER_FILES default. Opt out: VK_ADD_DRIVER_FILES="".
      # Invariant: install ships bin/anopticengine + bin/resources/shaders.
      mkEngine =
        {
          pkgs,
          stdenv,
          variant,
          buildType,
          headless ? false,
          wayland ? true,
          x11 ? true,
          tests ? false,
          sanitize ? "",
          softwareVulkan ? false,
          anyGpu ? false,
        }:
        let
          renderer = !headless;
          isDebug = buildType == "Debug";
          host = stdenv.hostPlatform;
          onOff = b: if b then "ON" else "OFF";
          vvl = vvlFor pkgs host;
          # Wrapper env: MoltenVK ICD on macOS, VK_LAYER_PATH for Debug renderers.
          wrapArgs =
            lib.optionals (renderer && host.isDarwin) [
              "--set-default"
              "VK_ICD_FILENAMES"
              "${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json"
            ]
            ++ lib.optionals (renderer && isDebug && vvl != null && !host.isWindows) [
              "--prefix"
              "VK_LAYER_PATH"
              ":"
              "${vvl}/share/vulkan/explicit_layer.d"
            ]
            # Additive to the host ICD scan.
            ++ lib.optionals (renderer && host.isLinux && anyGpu) [
              "--set-default"
              "VK_ADD_DRIVER_FILES"
              (mesaVkIcdPaths pkgs host)
            ];
          # Render libs on Linux: GLFW 3.4 links none of them — every one is dlopen()ed at
          # runtime (see postFixup). Doubling as buildInputs supplies headers plus the dev-shell
          # RUNPATH, and libGL covers glfw3.h's <GL/gl.h>. Xext/Xrender/Xxf86vm and libdecor are
          # GLFW-optional; absent they cost shaped windows, transparency, gamma, and Wayland
          # decorations, so ship them.
          # No `with pkgs`: the wayland parameter shadows pkgs.wayland.
          linuxRenderLibs = lib.optionals (renderer && host.isLinux) (
            [ pkgs.libGL ]
            ++ lib.optionals x11 (
              with pkgs;
              [
                libx11
                libxrandr
                libxinerama
                libxcursor
                libxi
                libxext
                libxrender
                libxxf86vm
              ]
            )
            ++ lib.optionals wayland [
              pkgs.wayland
              pkgs.libxkbcommon
              (libdecorSlim pkgs)
            ]
          );
        in
        stdenv.mkDerivation {
          pname = "anopticengine-${variant}";
          version = "0.0.1";
          src = self;

          nativeBuildInputs =
            (with pkgs.buildPackages; [
              cmake
              ninja
              pkg-config
            ])
            # llvm-ar/llvm-ranlib for the CheckIPOSupported probe (keeps ThinLTO).
            ++ lib.optionals (!host.isWindows) [ pkgs.buildPackages.llvmPackages_latest.llvm ]
            ++ lib.optionals renderer [ pkgs.buildPackages.shaderc ] # glslc
            # glslangValidator -gV: Debug shader debug info.
            ++ lib.optionals (renderer && isDebug) [ pkgs.buildPackages.glslang ]
            ++ lib.optionals (renderer && host.isLinux && wayland) [ pkgs.buildPackages.wayland-scanner ]
            # Renderer test suites need a display server for glfwInit(); Xvfb keeps it hermetic.
            ++ lib.optionals (tests && renderer && host.isLinux) [ pkgs.buildPackages.xvfb-run ]
            ++ lib.optionals (wrapArgs != [ ]) [ pkgs.buildPackages.makeWrapper ];

          buildInputs =
            lib.optionals host.isWindows [ pkgs.windows.pthreads ] # <pthread.h>
            ++ lib.optionals renderer (
              [
                pkgs.vulkan-headers
                pkgs.vulkan-loader
              ]
              ++ linuxRenderLibs
              ++ lib.optionals host.isDarwin [ pkgs.moltenvk ]
            );

          postUnpack =
            injectSubmodule "mimalloc" mimalloc-src
            + injectSubmodule "freetype" freetype-src
            + lib.optionalString renderer (injectSubmodule "glfw" glfw-src + injectSubmodule "cgltf" cgltf-src);

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=${buildType}"
          ]
          ++ lib.optional headless "-DANOPTIC_HEADLESS=ON"
          ++ lib.optional tests "-DANOPTIC_TESTS=ON"
          ++ lib.optional (sanitize != "") "-DANOPTIC_SANITIZE=${sanitize}"
          ++ lib.optionals (renderer && host.isLinux) [
            "-DGLFW_BUILD_WAYLAND=${onOff wayland}"
            "-DGLFW_BUILD_X11=${onOff x11}"
          ];

          hardeningDisable = lib.optionals isDebug fortifyOff;

          doCheck = tests;
          # ano_fs_userpath() needs a HOME (plus the macOS Application Support parents).
          # until-pass:2 absorbs sleep-precision flakes.
          # Renderer suites (tests-full): ctest under xvfb-run — the vk device tests reach
          # initVulkan() -> glfwInit(), and the sandbox has no display server of its own.
          checkPhase = ''
            runHook preCheck
            export HOME="$TMPDIR/anoptic-home"
            mkdir -p "$HOME/Library/Application Support"
            ${lib.optionalString (renderer && host.isLinux) ''xvfb-run -a -s "-screen 0 1280x720x24" \''}
            ctest --output-on-failure --repeat until-pass:2
            runHook postCheck
          '';

          # NIX_LDFLAGS -rpath: the ld-wrapper only rpaths -L dirs whose libs are -l linked,
          # and GLFW dlopen()s its platform libs — so bake their paths in at link time. The
          # vk test executables run in checkPhase (pre-shrink-rpath) and need this to reach
          # Xlib; the installed renderer gets the equivalent from the postFixup patchelf.
          # VK_LAYER_PATH: anotest_vk_compliance_layers/_sync assert that validation
          # layers intercept an intentional error, so the sandbox must supply them.
          env =
            lib.optionalAttrs (linuxRenderLibs != [ ]) {
              NIX_LDFLAGS = "-rpath ${lib.makeLibraryPath linuxRenderLibs}";
            }
            // lib.optionalAttrs softwareVulkan (
              {
                VK_ICD_FILENAMES = "${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.${host.parsed.cpu.name}.json";
              }
              // lib.optionalAttrs (vvl != null) {
                VK_LAYER_PATH = "${vvl}/share/vulkan/explicit_layer.d";
              }
            );

          # Fonts always, assets best-effort (an empty input warns).
          postInstall = lib.optionalString renderer (
            ''
              mkdir -p "$out/bin/resources"
              cp -r "$src/resources/fonts" "$out/bin/resources/fonts"
            ''
            + lib.optionalString (!tests) ''
              shopt -s nullglob
              staged=0
              for entry in ${anoptic-assets}/*; do
                case "$(basename "$entry")" in README*|LICENSE*|COPYING*) continue ;; esac
                cp -r "$entry" "$out/bin/"
                staged=1
              done
              if [ "$staged" -eq 0 ]; then
                echo "[anoptic] WARNING: assets input has no content — engine runs without demo assets." >&2
              fi
            ''
          );

          # GLFW 3.4 dlopen()s libX11/libwayland/libxkbcommon at runtime, so they never enter
          # DT_NEEDED: the ld-wrapper rpaths only -L dirs of -l linked libs (NIX_LDFLAGS above
          # covers the pre-install/checkPhase binaries), and fixupPhase's --shrink-rpath prunes
          # non-NEEDED entries from the installed ELF — glfwInit() then fails on the pure
          # artifact. Re-add them post-shrink. patchelf ships in the Linux stdenv; run it
          # before wrapProgram so the ELF, not its shell wrapper, is patched.
          postFixup =
            lib.optionalString (linuxRenderLibs != [ ]) ''
              patchelf --add-rpath "${lib.makeLibraryPath linuxRenderLibs}" "$out/bin/anopticengine"
            ''
            + lib.optionalString (wrapArgs != [ ]) ''
              wrapProgram "$out/bin/anopticengine" ${lib.escapeShellArgs wrapArgs}
            '';

          meta = {
            description = "Anoptic Engine — ${variant}";
            mainProgram = "anopticengine";
          };
        };

      perSystem =
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          host = pkgs.stdenv.hostPlatform;
          isLinux = host.isLinux;
          archTag = if host.isx86_64 then "x64" else "aarch64";
          hostTag = (if isLinux then "linux" else "macos") + "-" + archTag;

          # Latest clang in the pin (full C23): lld on Linux, ld64 via the darwin llvm stdenv.
          llvmLatest = pkgs.llvmPackages_latest;
          engineStdenv =
            if isLinux then
              pkgs.overrideCC pkgs.clangStdenv (llvmLatest.clang.override { bintools = llvmLatest.bintools; })
            else
              llvmLatest.stdenv;

          mkHost =
            args:
            mkEngine (
              {
                inherit pkgs;
                stdenv = engineStdenv;
              }
              // args
            );

          # Windows cross: MinGW-w64 ucrt64 (C11 timespec_get). x86_64-linux hosts only.
          crossPkgs = pkgs.pkgsCross.ucrt64;
          mkWin =
            args:
            mkEngine (
              {
                pkgs = crossPkgs;
                stdenv = crossPkgs.stdenv;
              }
              // args
            );

          native = mkVariants mkHost (
            {
              "release-${hostTag}" = {
                buildType = "Release";
              };
              "debug-${hostTag}" = {
                buildType = "Debug";
              };
              "release-headless-${hostTag}" = {
                buildType = "Release";
                headless = true;
              };
              "debug-headless-${hostTag}" = {
                buildType = "Debug";
                headless = true;
              };
            }
            # Single-backend diets. The unsuffixed build carries both, selected at runtime.
            // lib.optionalAttrs isLinux {
              "release-${hostTag}-wayland" = {
                buildType = "Release";
                x11 = false;
              };
              "release-${hostTag}-x11" = {
                buildType = "Release";
                wayland = false;
              };
              "debug-${hostTag}-wayland" = {
                buildType = "Debug";
                x11 = false;
              };
              "debug-${hostTag}-x11" = {
                buildType = "Debug";
                wayland = false;
              };
              "release-${hostTag}-anygpu" = {
                buildType = "Release";
                anyGpu = true;
              };
              "debug-${hostTag}-anygpu" = {
                buildType = "Debug";
                anyGpu = true;
              };
            }
          );

          windows = lib.optionalAttrs (system == "x86_64-linux") (
            let
              w = mkVariants mkWin {
                release-windows-x64 = {
                  buildType = "Release";
                };
                debug-windows-x64 = {
                  buildType = "Debug";
                };
                release-headless-windows-x64 = {
                  buildType = "Release";
                  headless = true;
                };
                debug-headless-windows-x64 = {
                  buildType = "Debug";
                  headless = true;
                };
              };
            in
            w // { release-wsl = w.release-windows-x64; }
          );

          # Host-resolved short names.
          aliases = {
            default = native."release-${hostTag}";
            release = native."release-${hostTag}";
            debug = native."debug-${hostTag}";
            release-headless = native."release-headless-${hostTag}";
            debug-headless = native."debug-headless-${hostTag}";
            headless = native."release-headless-${hostTag}";
          }
          // lib.optionalAttrs isLinux {
            anygpu = native."release-${hostTag}-anygpu";
          };

          # Sandbox test suites. Building one runs it. Sanitized suites run headless.
          # GPU-real sanitizer runs are `nix run -- 6|7`.
          # tests-full (experimental): full suite incl. Vulkan device tests on lavapipe.
          checks = mkVariants mkHost (
            {
              tests-headless = {
                buildType = "Debug";
                headless = true;
                tests = true;
              };
            }
            // lib.optionalAttrs isLinux {
              tests-asan = {
                buildType = "Debug";
                headless = true;
                tests = true;
                sanitize = "asan";
              };
              tests-tsan = {
                buildType = "Debug";
                headless = true;
                tests = true;
                sanitize = "tsan";
              };
              tests-full = {
                buildType = "Debug";
                tests = true;
                softwareVulkan = true;
              };
            }
          );

          # nixglhost: harvests the host NVIDIA userspace at runtime (non-NixOS).
          nixglhost = if isLinux then nix-gl-host.packages.${system}.default else null;

          # nix run [-- N]: the impure entry. Fatal pin check, submodule/asset supply,
          # then ./build.sh N in the dev shell.
          runWrapper = pkgs.writeShellApplication {
            name = "anoptic-build";
            runtimeInputs = [
              pkgs.git
              pkgs.coreutils
            ];
            text = ''
              root="$(git rev-parse --show-toplevel 2>/dev/null || true)"
              if [ -z "$root" ] || [ ! -f "$root/build.sh" ]; then
                echo "[anoptic] not inside the anoptic-engine work tree." >&2
                exit 1
              fi
              cd "$root" || exit 1
              mode="''${1:-1}"

              fail=0
              for pair in ${pinList}; do
                p="''${pair%%=*}" want="''${pair#*=}"
                rec="$(git ls-tree HEAD "$p" 2>/dev/null | awk '{ print $3 }')"
                if [ -n "$rec" ] && [ "$rec" != "$want" ]; then
                  echo "[anoptic] FATAL: $p is at $rec but flake.nix pins $want." >&2
                  fail=1
                fi
              done
              if [ "$fail" -ne 0 ]; then
                echo "[anoptic] run 'git submodule update --init --recursive' and commit the corrected pointer, or update the flake pin if the bump is intentional." >&2
                exit 1
              fi

              for pair in ${pinList}; do
                p="''${pair%%=*}"
                if [ -z "$(ls -A "$p" 2>/dev/null)" ]; then
                  echo "[anoptic] fetching submodules..."
                  git submodule update --init --recursive
                  break
                fi
              done

              # Provision assets/ only when absent or empty. User content is left alone.
              if [ -z "$(ls -A assets 2>/dev/null)" ]; then
                if GIT_SSH_COMMAND="ssh -o BatchMode=yes -o ConnectTimeout=5" \
                    git clone --depth 1 git@github.com:Anoptic-Games/assets.git assets >/dev/null 2>&1; then
                  echo "[anoptic] assets: private repo."
                else
                  echo "[anoptic] WARNING: private assets unreachable — staging public assets-free." >&2
                  cp -r ${anoptic-assets}/. assets
                  chmod -R u+w assets
                fi
              fi

              # WSL has no in-guest render target. Point at .#release-wsl for the renderer.
              is_wsl=0
              if [ -r /proc/version ] && grep -qi microsoft /proc/version; then
                is_wsl=1
                echo "[anoptic] WSL detected — renderer profiles build but cannot display; 'nix run -- 3' runs the headless engine in-guest, 'nix build .#release-wsl' emits the Windows renderer exe."
              fi

              # Build the requested profile in the dev shell.
              nix develop "$root" --command ./build.sh "$mode"

              # Launch the freshly built engine for the plain build profiles: renderer (1|2) and
              # headless console (3). Profiles 4-8 already ran their suite inside build.sh, so
              # there is nothing to launch. build_dir mirrors build.sh's mode->dir mapping; the
              # binary self-locates its resources via /proc/self/exe, and the dev shell supplies
              # VK_ICD_FILENAMES (MoltenVK) and VK_LAYER_PATH (Debug validation). Renderer builds
              # have no display target inside WSL, so 1|2 stop after building there; the headless
              # engine needs neither display nor GPU and launches anywhere.
              case "$mode" in
                1) build_dir="Release" ;;
                2) build_dir="Debug" ;;
                3) build_dir="Headless" ;;
                *) build_dir="" ;;
              esac
              if [ "$is_wsl" -eq 1 ] && [ "$mode" != "3" ]; then
                build_dir=""
              fi
              if [ -n "$build_dir" ]; then
                bin="$root/build/$build_dir/anopticengine"
                if [ ! -x "$bin" ]; then
                  echo "[anoptic] no runnable engine at $bin — the renderer was skipped (no Vulkan SDK?); see the build log above." >&2
                  exit 1
                fi
                echo "[anoptic] launching $bin"
                ${lib.optionalString isLinux ''
                  # Foreign distro: stage store mesa ICDs, bridge NVIDIA via nixglhost.
                  if [ ! -e /run/opengl-driver ] && [ "$mode" != "3" ]; then
                    export VK_ADD_DRIVER_FILES="''${VK_ADD_DRIVER_FILES-${mesaVkIcdPaths pkgs host}}"
                    if [ -e /proc/driver/nvidia/version ]; then
                      echo "[anoptic] NVIDIA kernel module detected — bridging via nixglhost."
                      exec nix develop "$root" --command ${nixglhost}/bin/nixglhost "$bin"
                    fi
                  fi
                ''}
                exec nix develop "$root" --command "$bin"
              fi
            '';
          };

          # Pure engine on the host NVIDIA driver via nixglhost.
          nvidiaLaunchers = lib.optionalAttrs isLinux (
            lib.mapAttrs
              (
                name: engine:
                pkgs.writeShellApplication {
                  name = "anoptic-${name}";
                  text = ''
                    exec ${nixglhost}/bin/nixglhost ${lib.getExe engine} "$@"
                  '';
                }
              )
              {
                nvidia = native."release-${hostTag}";
                nvidia-debug = native."debug-${hostTag}";
              }
          );

          # llvm: llvm-ar/-ranlib keep ThinLTO in-shell. lldb version-matched to clang.
          shellTools =
            (with pkgs; [
              cmake
              ninja
              pkg-config
              shaderc
              glslang
              git
            ])
            ++ [
              llvmLatest.llvm
              llvmLatest.lldb
            ];
          vvlShell = vvlFor pkgs host;

          # The dlopen()ed render libs (see mkEngine's linuxRenderLibs): GLFW never -l links
          # them, so the ld-wrapper adds no RUNPATH — bake one in via NIX_LDFLAGS so the
          # build.sh binaries can init GLFW outside the shell too (e.g. under nixglhost).
          shellRenderLibs =
            (with pkgs; [
              libGL
              libx11
              libxrandr
              libxinerama
              libxcursor
              libxi
              libxext
              libxrender
              libxxf86vm
              wayland
              libxkbcommon
            ])
            ++ [ (libdecorSlim pkgs) ];

          devShells = {
            default =
              if isLinux then
                (pkgs.mkShell.override { stdenv = engineStdenv; }) (
                  {
                    name = "anoptic-linux";
                    hardeningDisable = fortifyOff;
                    nativeBuildInputs = shellTools ++ [ pkgs.wayland-scanner ];
                    buildInputs =
                      (with pkgs; [
                        vulkan-headers
                        vulkan-loader
                      ])
                      ++ shellRenderLibs
                      ++ lib.optional (vvlShell != null) vvlShell;
                    NIX_LDFLAGS = "-rpath ${lib.makeLibraryPath shellRenderLibs}";
                    # GPU-less full-suite runs: VK_ICD_FILENAMES=$ANO_LAVAPIPE_ICD ctest ...
                    ANO_LAVAPIPE_ICD = "${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.${host.parsed.cpu.name}.json";
                    shellHook = ''
                      echo "[anoptic] Linux target — $(clang --version | head -1)"
                    ''
                    + submodulePinWarn;
                  }
                  // lib.optionalAttrs (vvlShell != null) {
                    VK_LAYER_PATH = "${vvlShell}/share/vulkan/explicit_layer.d";
                  }
                )
              else
                (pkgs.mkShell.override { stdenv = engineStdenv; }) (
                  {
                    name = "anoptic-macos";
                    hardeningDisable = fortifyOff;
                    nativeBuildInputs = shellTools;
                    buildInputs =
                      (with pkgs; [
                        vulkan-headers
                        vulkan-loader
                        moltenvk
                      ])
                      ++ lib.optional (vvlShell != null) vvlShell;
                    VK_ICD_FILENAMES = "${pkgs.moltenvk}/share/vulkan/icd.d/MoltenVK_icd.json";
                    shellHook = ''
                      echo "[anoptic] macOS target — $(clang --version | head -1)"
                    ''
                    + submodulePinWarn;
                  }
                  // lib.optionalAttrs (vvlShell != null) {
                    VK_LAYER_PATH = "${vvlShell}/share/vulkan/explicit_layer.d";
                  }
                );
          }
          // lib.optionalAttrs (system == "x86_64-linux") {
            # Interactive cross env. Artifact path: nix build .#release-wsl
            windows = crossPkgs.mkShell {
              name = "anoptic-windows";
              hardeningDisable = fortifyOff;
              nativeBuildInputs = shellTools;
              buildInputs = [
                crossPkgs.vulkan-headers
                crossPkgs.vulkan-loader
                crossPkgs.windows.pthreads
              ];
              shellHook = ''
                echo "[anoptic] Windows target — $($CC --version | head -1)"
                echo "[anoptic] configure with: cmake \$cmakeFlags -G Ninja -S . -B build/Windows"
              ''
              + submodulePinWarn;
            };
          };
        in
        {
          packages = native // windows // aliases // checks // nvidiaLaunchers;
          inherit checks devShells;
          apps = {
            default = {
              type = "app";
              program = lib.getExe runWrapper;
              meta.description = "Anoptic Engine — C23 game engine for million-entity simulation";
            };
          }
          // lib.mapAttrs (name: launcher: {
            type = "app";
            program = lib.getExe launcher;
            meta.description = "Anoptic Engine — pure ${name} launch on the host NVIDIA driver";
          }) nvidiaLaunchers;
        };
      # Evaluate each system once, then project the output types.
      perSys = forAllSystems perSystem;
    in
    {
      packages = lib.mapAttrs (_: s: s.packages) perSys;
      checks = lib.mapAttrs (_: s: s.checks) perSys;
      devShells = lib.mapAttrs (_: s: s.devShells) perSys;
      apps = lib.mapAttrs (_: s: s.apps) perSys;
      # nixfmt-tree: tree-mode `nix fmt`.
      formatter = forAllSystems (s: nixpkgs.legacyPackages.${s}.nixfmt-tree);
    };
}
