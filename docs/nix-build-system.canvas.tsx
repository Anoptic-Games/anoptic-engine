import {
  Callout,
  Code,
  Divider,
  H1,
  H2,
  Stack,
  Table,
  Text,
  computeDAGLayout,
  useHostTheme,
} from "cursor/canvas";

const MONO = "ui-monospace, 'Cascadia Code', Consolas, monospace";

type FlowNode = {
  id: string;
  label: string;
  detail?: string;
  mono?: boolean;
  emphasis?: boolean;
};

type FlowEdge = { from: string; to: string; label?: string };

function FlowDiagram(props: {
  markerId: string;
  nodes: FlowNode[];
  edges: FlowEdge[];
  direction?: "vertical" | "horizontal";
  nodeWidth?: number;
  nodeHeight?: number;
  rankGap?: number;
  nodeGap?: number;
}) {
  const theme = useHostTheme();
  const nodeWidth = props.nodeWidth ?? 250;
  const nodeHeight = props.nodeHeight ?? 66;
  const layout = computeDAGLayout({
    nodes: props.nodes.map((n) => ({ id: n.id })),
    edges: props.edges.map((e) => ({ from: e.from, to: e.to })),
    direction: props.direction ?? "vertical",
    nodeWidth,
    nodeHeight,
    rankGap: props.rankGap ?? 48,
    nodeGap: props.nodeGap ?? 24,
  });
  const specById = new Map(props.nodes.map((n) => [n.id, n]));
  const labelFor = (from: string, to: string) =>
    props.edges.find((e) => e.from === from && e.to === to)?.label;

  return (
    <div style={{ overflowX: "auto" }}>
      <div
        style={{
          position: "relative",
          width: layout.width,
          height: layout.height,
        }}
      >
        <svg
          width={layout.width}
          height={layout.height}
          style={{ position: "absolute", inset: 0 }}
        >
          <defs>
            <marker
              id={props.markerId}
              markerWidth="8"
              markerHeight="8"
              refX="7"
              refY="4"
              orient="auto"
            >
              <path d="M0,0 L8,4 L0,8 Z" fill={theme.text.tertiary} />
            </marker>
          </defs>
          {layout.edges.map((e, i) => {
            const d =
              layout.direction === "horizontal"
                ? `M ${e.sourceX},${e.sourceY} C ${(e.sourceX + e.targetX) / 2},${e.sourceY} ${(e.sourceX + e.targetX) / 2},${e.targetY} ${e.targetX},${e.targetY}`
                : `M ${e.sourceX},${e.sourceY} C ${e.sourceX},${(e.sourceY + e.targetY) / 2} ${e.targetX},${(e.sourceY + e.targetY) / 2} ${e.targetX},${e.targetY}`;
            return (
              <path
                key={i}
                d={d}
                fill="none"
                stroke={theme.text.tertiary}
                strokeWidth={1.2}
                markerEnd={`url(#${props.markerId})`}
              />
            );
          })}
        </svg>
        {layout.edges.map((e, i) => {
          const label = labelFor(e.from, e.to);
          if (!label) return null;
          return (
            <div
              key={`label-${i}`}
              style={{
                position: "absolute",
                left: (e.sourceX + e.targetX) / 2,
                top: (e.sourceY + e.targetY) / 2,
                transform: "translate(-50%, -50%)",
                background: theme.bg.editor,
                color: theme.text.tertiary,
                fontSize: 10,
                lineHeight: "12px",
                padding: "1px 5px",
                borderRadius: 4,
                whiteSpace: "nowrap",
              }}
            >
              {label}
            </div>
          );
        })}
        {layout.nodes.map((n) => {
          const spec = specById.get(n.id);
          if (!spec) return null;
          return (
            <div
              key={n.id}
              style={{
                position: "absolute",
                left: n.x,
                top: n.y,
                width: nodeWidth,
                height: nodeHeight,
                boxSizing: "border-box",
                border: `1px solid ${spec.emphasis ? theme.accent.primary : theme.stroke.secondary}`,
                borderRadius: 6,
                background: theme.bg.elevated,
                padding: "7px 10px",
                display: "flex",
                flexDirection: "column",
                justifyContent: "center",
                gap: 3,
                overflow: "hidden",
              }}
            >
              <span
                style={{
                  fontSize: 12,
                  fontWeight: 600,
                  color: theme.text.primary,
                  lineHeight: "14px",
                  fontFamily: spec.mono ? MONO : undefined,
                }}
              >
                {spec.label}
              </span>
              {spec.detail ? (
                <span
                  style={{
                    fontSize: 10.5,
                    color: theme.text.secondary,
                    lineHeight: "13px",
                  }}
                >
                  {spec.detail}
                </span>
              ) : null}
            </div>
          );
        })}
      </div>
    </div>
  );
}

function Caption({ children }: { children?: string }) {
  return (
    <Text size="small" tone="tertiary">
      {children}
    </Text>
  );
}

const dataSourceNodes: FlowNode[] = [
  {
    id: "flake",
    label: "flake.nix + flake.lock",
    detail: "the recipes, plus exact content hashes for every input",
    mono: true,
  },
  {
    id: "self",
    label: "self — your git tree",
    detail: "committed files only: no submodule contents, no assets/, no build/",
  },
  {
    id: "nixpkgs",
    label: "nixpkgs @ b5aa0fbd",
    detail: "clang 21 + lld, cmake, ninja, glslc, MinGW cross, MoltenVK",
    mono: true,
  },
  {
    id: "subs",
    label: "4 pinned submodule srcs",
    detail: "mimalloc · freetype · glfw · cgltf, revs matching .gitmodules",
  },
  {
    id: "eval",
    label: "pure evaluation → .drv",
    detail: "an exact build plan, hashed over every input bit",
  },
  {
    id: "cache",
    label: "store lookup by hash",
    detail: "same hash seen before? reuse instantly — otherwise build",
  },
  {
    id: "build",
    label: "sandboxed whole build",
    detail: "empty dir, no network: inject submodules, cmake + ninja + ThinLTO, install",
  },
  {
    id: "out",
    label: "/nix/store/<hash>-anopticengine-*",
    detail: "immutable output — ./result symlink points at it",
    mono: true,
    emphasis: true,
  },
];

const dataSourceEdges: FlowEdge[] = [
  { from: "flake", to: "self", label: "fetch" },
  { from: "flake", to: "nixpkgs", label: "fetch" },
  { from: "flake", to: "subs", label: "fetch" },
  { from: "self", to: "eval" },
  { from: "nixpkgs", to: "eval" },
  { from: "subs", to: "eval" },
  { from: "eval", to: "cache" },
  { from: "cache", to: "build", label: "miss" },
  { from: "build", to: "out" },
];

const routingNodes: FlowNode[] = [
  { id: "c-build", label: "nix build", mono: true },
  { id: "c-renderer", label: "nix build .#renderer", mono: true },
  { id: "c-develop", label: "nix develop", mono: true },
  { id: "c-windows", label: "nix develop .#windows", mono: true },
  { id: "c-bat", label: "build.bat  (no Nix)", mono: true },
  {
    id: "a-default",
    label: "packages.<system>.default",
    detail: "headless engine · Release + ThinLTO · Linux and macOS",
    mono: true,
  },
  {
    id: "a-renderer",
    label: "packages.x86_64-linux.renderer",
    detail: "Vulkan renderer + compiled shaders",
    mono: true,
  },
  {
    id: "a-shell",
    label: "devShells.<system>.default",
    detail: "toolchain env only — nothing is built yet",
    mono: true,
  },
  {
    id: "a-cross",
    label: "devShells.x86_64-linux.windows",
    detail: "MinGW-w64 ucrt64 cross env, $cmakeFlags exported",
    mono: true,
  },
  {
    id: "a-msys",
    label: "MSYS2 clang64 on PATH",
    detail: "native Windows — Nix never enters the picture",
  },
  {
    id: "r-default",
    label: "./result/bin/anopticengine",
    detail: "immutable, headless, no GPU or display needed",
    mono: true,
    emphasis: true,
  },
  {
    id: "r-renderer",
    label: "./result/bin + resources/shaders",
    detail: "self-contained; stage assets/ beside the exe to run the demo",
    mono: true,
    emphasis: true,
  },
  {
    id: "r-shell",
    label: "./build.sh 1–7 → build/<label>/",
    detail: "Debug, CTest, ASan/TSan, release benchmarks",
    mono: true,
    emphasis: true,
  },
  {
    id: "r-cross",
    label: "build/Windows/anopticengine.exe",
    detail: "cmake $cmakeFlags; runs on the Windows host via interop",
    mono: true,
    emphasis: true,
  },
  {
    id: "r-bat",
    label: "build\\<label>\\anopticengine.exe",
    detail: "static-linked, self-contained (only vulkan-1.dll stays dynamic)",
    mono: true,
    emphasis: true,
  },
];

const routingEdges: FlowEdge[] = [
  { from: "c-build", to: "a-default" },
  { from: "c-renderer", to: "a-renderer" },
  { from: "c-develop", to: "a-shell" },
  { from: "c-windows", to: "a-cross" },
  { from: "c-bat", to: "a-msys" },
  { from: "a-default", to: "r-default" },
  { from: "a-renderer", to: "r-renderer" },
  { from: "a-shell", to: "r-shell" },
  { from: "a-cross", to: "r-cross" },
  { from: "a-msys", to: "r-bat" },
];

export default function NixBuildSystemRundown() {
  return (
    <Stack gap={20} style={{ maxWidth: 960 }}>
      <Stack gap={8}>
        <H1>Anoptic × Nix — how the build system actually works</H1>
        <Text tone="secondary">
          Goal recap: flat whole-program compiles with LTO and fast linkers, no
          incremental builds, and Nix owning both dependencies and setup. The
          key insight is that this goal is not something bolted onto Nix — it
          IS Nix's native execution model. A derivation cannot build
          incrementally: the sandbox starts empty every time, and the unit of
          caching is the whole immutable output, keyed by the hash of every
          input. `ano_scrub` exists only to impose that same discipline on the
          mutable `build/` trees the dev shells use.
        </Text>
      </Stack>

      <Stack gap={10}>
        <H2>1 · Where Nix gets its data</H2>
        <Text tone="secondary">
          Everything a build can see is declared in `flake.nix` and pinned in
          `flake.lock`. There are exactly three kinds of source: your own git
          tree (`self`), the pinned `nixpkgs` snapshot that provides every
          tool, and four pinned tarballs standing in for the git submodules
          (which are deliberately absent from `self`). All of it is fetched
          into the read-only, content-addressed `/nix/store` before any
          compiler runs.
        </Text>
        <FlowDiagram
          markerId="arrow-sources"
          nodes={dataSourceNodes}
          edges={dataSourceEdges}
          direction="vertical"
          nodeWidth={260}
          nodeHeight={68}
          rankGap={44}
        />
        <Caption>
          Source: flake.nix / flake.lock in the repo root. The cache-hit path
          skips the build box entirely — a second `nix build` with nothing
          changed returns in milliseconds.
        </Caption>
      </Stack>

      <Stack gap={10}>
        <H2>2 · What gets built, and why</H2>
        <Text tone="secondary">
          Two independent mechanisms decide this. <Text weight="semibold">
          What</Text>: the attribute path. Each command names an output
          attribute, and Nix fills in <Code>{"<system>"}</Code> from your host (that is the
          platform detection — `x86_64-linux` and `aarch64-darwin` each carry
          their own branch of the outputs tree). The one thing deliberately
          not auto-detected is the <Text italic>target</Text>: a Linux host
          can build both the Linux engine and the Windows cross build, so the
          flake makes you name `.#windows` — the flake-idiomatic "argument".
        </Text>
        <Text tone="secondary">
          <Text weight="semibold">Why (or whether)</Text>: the derivation
          hash. Change any bit of any input — a source file, a compiler flag,
          the nixpkgs rev — and the `.drv` hash changes, which means a new
          store path, which means a build. Nothing scans timestamps and no
          incremental state exists to go stale; an unchanged hash is an
          instant cache hit.
        </Text>
      </Stack>

      <Stack gap={10}>
        <H2>3 · What each invocation routes to</H2>
        <FlowDiagram
          markerId="arrow-routing"
          nodes={routingNodes}
          edges={routingEdges}
          direction="horizontal"
          nodeWidth={248}
          nodeHeight={64}
          rankGap={56}
          nodeGap={20}
        />
        <Caption>
          Left: what you type. Middle: the flake output attribute (or PATH
          lookup) it resolves to. Right: the artifact or working state you end
          up with.
        </Caption>
        <Table
          headers={["Command", "Resolves to", "You get", "Where it applies"]}
          rows={[
            [
              <Code>nix build</Code>,
              <Code>{"packages.<system>.default"}</Code>,
              "Headless engine, ./result/bin/anopticengine",
              "Any Linux, macOS, or WSL host — the quickstart",
            ],
            [
              <Code>nix build .#renderer</Code>,
              <Code>packages.x86_64-linux.renderer</Code>,
              "Renderer + shaders in ./result",
              "Linux with a real GPU driver",
            ],
            [
              <Code>nix develop</Code>,
              <Code>{"devShells.<system>.default"}</Code>,
              "Shell: clang+lld, cmake, ninja, glslc (+ MoltenVK on macOS)",
              "The dev loop; run ./build.sh N inside it",
            ],
            [
              <Code>nix develop .#windows</Code>,
              <Code>devShells.x86_64-linux.windows</Code>,
              "MinGW-w64 cross shell, cross setup in $cmakeFlags",
              "Building a Windows .exe from WSL/Linux",
            ],
            [
              <Code>nix fmt</Code>,
              <Code>{"formatter.<system>"}</Code>,
              "nixfmt over the flake",
              "Housekeeping",
            ],
            [
              <Code>build.bat N</Code>,
              "MSYS2 clang64 on PATH (no Nix)",
              "build\\<label>\\anopticengine.exe",
              "Native Windows — Nix does not run there",
            ],
          ]}
        />
      </Stack>

      <Stack gap={10}>
        <H2>4 · Toolchain and LTO matrix</H2>
        <Text tone="secondary">
          The "flat compile, LTO, fast linker" policy is enforced in the root
          `CMakeLists.txt` (linker probe + `check_ipo_supported`), and the
          flake's job is to guarantee the right tools are on PATH so that
          probe resolves the same way everywhere. Clang/LLVM is the default
          toolkit; GCC is the designated fallback on exactly one path.
        </Text>
        <Table
          headers={["Target", "Compiler", "Linker", "LTO", "Provisioned by"]}
          rows={[
            [
              "Linux (packages + dev shell)",
              "clang (nixpkgs LLVM)",
              "lld",
              "ThinLTO",
              <Code>clangLldStdenv</Code>,
            ],
            [
              "macOS, Apple Silicon",
              "clang (nixpkgs LLVM — full C23)",
              "ld64 (Apple policy, no override)",
              "ThinLTO",
              "darwin stdenv / dev shell",
            ],
            [
              "Windows cross (.#windows)",
              "MinGW-w64 gcc, ucrt64",
              "GNU ld (mold can't emit PE/COFF; lld can't load gcc's LTO plugin)",
              "fat -flto via gcc-ar",
              <Code>pkgsCross.ucrt64</Code>,
            ],
            [
              "Windows native (build.bat)",
              "MSYS2 clang64",
              "lld",
              "ThinLTO",
              "MSYS2 — no Nix",
            ],
          ]}
        />
      </Stack>

      <Stack gap={10}>
        <H2>5 · What build.sh / build.bat are still for</H2>
        <Text tone="secondary">
          The packages answer "give me the artifact"; the scripts answer "let
          me work on it". They are not legacy — they own everything a
          hermetic package build can't do: Debug configurations, running
          CTest, sanitizers, and iterating against your working tree
          (including gitignored `assets/`). What <Text italic>is</Text>{" "}
          effectively legacy is running `build.sh` outside any Nix shell.
        </Text>
        <Table
          headers={["Script / context", "Role"]}
          rows={[
            [
              <Code>build.sh</Code>,
              "The dev-loop driver: profiles 1–7 map to Release / Debug / Tests / ASan / TSan / Headless / Release-tests. Runs ano_scrub before every build — the whole-build policy applied to the repo's mutable build/ trees.",
            ],
            [
              "…inside nix develop",
              "The intended home. The shell puts the pinned clang+lld, cmake, ninja, glslc on PATH; the script's toolchain files resolve against them. Linux shell: headless/tests/sanitizers (6, 5, 4). macOS shell: the full matrix including the renderer (1).",
            ],
            [
              "…outside Nix",
              "The bare-metal fallback: Homebrew LLVM on macOS, system clang on Linux. Works, but you own the dependency versions.",
            ],
            [
              <Code>build.bat</Code>,
              "The only native-Windows path, and therefore first-class, not legacy: Nix does not run on native Windows. Finds MSYS2 clang64, mirrors build.sh's profiles, links -static.",
            ],
          ]}
        />
      </Stack>

      <Divider />

      <Stack gap={10}>
        <H2>Why this layout is the idiomatic Nix way</H2>
        <Text tone="secondary">
          The flake follows the standard schema and uses each output kind for
          exactly what it is meant for: `packages` for final artifacts (built
          hermetically, cached by hash), `devShells` for reproducing the
          human's environment (tools on PATH, nothing built), one lockfile
          pinning every input including vendored submodule sources, per-system
          attributes for host detection, and explicit attribute names — never
          heuristics — for anything a host can't infer, like cross targets.
          Dependency handling and painless setup fall out of the same
          mechanism: there is nothing to install besides Nix itself, because
          the toolchain is data, pinned in the lockfile like everything else.
        </Text>
        <Callout tone="warning" title="Sharp edges to remember">
          <Stack gap={4}>
            <Text size="small">
              `assets/` is gitignored, so it is absent from `self` — the
              packaged renderer aborts at the model load unless you stage
              assets beside the exe. The dev-shell flow reads them from your
              working tree and is unaffected.
            </Text>
            <Text size="small">
              Flakes need `experimental-features = nix-command flakes` in
              nix.conf — a flake cannot self-enable this.
            </Text>
            <Text size="small">
              `./result` is only a symlink into the store (gitignored);
              delete it freely, and `nix store gc` reclaims unused outputs.
            </Text>
          </Stack>
        </Callout>
      </Stack>
    </Stack>
  );
}
