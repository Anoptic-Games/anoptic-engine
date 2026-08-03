# Device recorder profile experiment 〜 2026-08-03

## Decision

Retain and commit. The experiment removes repeated device-capability branches from the hot per-view draw seam, strengthens the closed profile contract, reduces emitted recorder code substantially, reduces the final Windows executable, and measures runtime-neutral in the minimum controlled paired comparison.

## Hypothesis

record_graphics_pass previously re-evaluated mesh versus vertex geometry, task-shader participation, and counted versus uncounted indirect submission inside every graphics pass. Those facts are fixed after Vulkan device creation. Specializing only that seam should prune the branches without multiplying the complete recorder across async Hi-Z, async light-cull, async text, render-pass, and pipeline combinations.

## Implementation

AnoDrawGeometry and AnoDrawSubmission are strong enums. Six AnoDrawProfile enumerators carry typed AnoDrawProfileSpec annotations for vertex, mesh, and task geometry crossed with counted and uncounted submission.

GCC 16.1 reflection enumerates the profile declarations through std::define_static_array and template for. consteval validation proves that every enumerator has exactly one profile or sentinel annotation, every enum value belongs to its declared domain, every geometry/submission cell occurs exactly once, and the complete 3 by 2 matrix is populated.

Templates provide the reusable record_draw_kernel implementation. Reflection instantiates exactly the six declared specializations and constructs a 48-byte function table. ano_select_view_draw_profile selects one pointer after capability gates and mesh entry-point loading. record_graphics_pass then performs one indirect call per non-empty graphics pass with no mesh, task, or indirect-count decision tree.

The async submission implementation is unchanged. No 18-way or 30-way whole-recorder cross-product was instantiated.

## Compile-time rejection

A production-schema copy was deliberately corrupted so mesh/uncounted appeared twice and mesh/counted disappeared. GCC rejected the translation unit while evaluating ano_validate_draw_profiles. Missing profiles, duplicate profiles, invalid enum-domain values, malformed annotations, and an unpopulated generated function cell therefore fail the build.

## Emitted code

The isolated recorder objects used identical GCC 16.1 Release flags with -O3 and reflection, omitting LTO so symbol sizes remained directly inspectable. The final executable comparison used the canonical Nix MinGW GCC 16.1 Release build with LTO.

| Surface | Before | Reflected profiles | Delta |
| --- | ---: | ---: | ---: |
| record_views object text | 16,957 B | 12,693 B | -4,264 B (-25.1%) |
| ano_record_views | 15,524 B | 9,995 B | -5,529 B (-35.6%) |
| Six specialized kernels | 0 B | 889 B | +889 B |
| Function table and selected pointer | 0 B | 56 B | +56 B |
| Windows executable text | 2,236,544 B | 2,232,936 B | -3,608 B |
| Windows executable file | 2,700,117 B | 2,697,730 B | -2,387 B |

The specialization therefore improved instruction layout rather than purchasing branch removal with code duplication.

## Runtime comparison

The official Windows driver ran two balanced adjacent pairs at one representative 2560x1440 resolution, 30 seconds per fresh process, with identical assets and ANO_SHADOW_BUDGET=2. All four runs were foreground verified and agreed on realized extent, 642.3 MiB swap allocation, eight shadow frusta, active mesh/task/async paths, and CPU/present-bound classification.

| A fps | B fps | B - A | Paired 95% CI | Verdict |
| ---: | ---: | ---: | ---: | --- |
| 520.57 | 523.52 | +0.57% | -5.30% to +6.43% | Neutral |

The confidence interval contains zero. The +0.57% point estimate is not claimed as an improvement. Full raw rows and environment details are preserved in the [paired benchmark](../benchmarks/2026-08-03-hyper-c-reflection-draw-profiles-paired-1440p.md).

## Verification

- Full native Release engine build passed.
- All 29 enabled existing tests passed.
- Canonical nix build .#release-wsl passed.
- The benchmarked executable and the final polished executable are byte-identical.
- The deliberately invalid profile inventory failed compilation.

## Boundary

This result justifies the narrow draw-profile seam. It does not justify templating the entire frame recorder, combining draw profiles with async topology, or spreading device profiles into algorithms whose state changes per frame.
