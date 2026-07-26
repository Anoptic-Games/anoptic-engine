# Bugs!

Grouped by: 
- Module / Subsystem (see docs/conventions.md for a definition)
-- Within each module: category.

## Tags

Each entry carries one or more root-cause tags on the bullet line(s) beneath it, so the census reads by shape as well as by module. Three root causes dominate the board; the rest are smaller shared shapes or 1:1 singletons.

An entry prefixed `[X] Fixed` has been repaired; its text is kept verbatim as the record of what was wrong, and a closing `fix (date)` bullet says what was done. An entry carrying a `wontfix (date)` bullet instead is a finding whose analysis stands but whose repair the owner declined, with the reason on that bullet; it counts as unremediated. A `fenced (date)` bullet marks an open entry a pass deliberately worked around rather than fixed, with the reason and the guard's expected colour on that bullet; it too counts as unremediated. None of the three is a tag, and all are excluded from every tag count.

- checked-arithmetic 〜 an integer wrap (multiply, add, or subtract) slips past the guard or the allocation-sizing it should have been caught by; one shared checked-arithmetic helper retires the whole family.
- no-abort 〜 a failure that should stop the operation does not: ano_log(ANO_FATAL) logs and returns, or a callee reports unconditional success or hands back an uninitialised out-param, so the caller's error arm is dead.
- seam-validation 〜 a value crossing a documented seam (public API, bridge, config) is trusted as valid or used as an index without its domain being checked.
- ownership-leak 〜 a resource acquired earlier is never discharged on a failure return or at teardown; the discharge-side facet of the seam root cause.
- fixed-array-overflow 〜 an unbounded counter or index writes past a fixed-size stack array while a sibling bound elsewhere is guarded.
- partial-publish 〜 a batch operation publishes per-element state as it walks, then fails mid-batch with only a scalar return, leaving the prefix live and aliasable.
- copy-paste-error 〜 code cloned from a sibling with one token left unchanged, a duplicated argument or predicate.
- feature-list-drift 〜 a per-feature resource property maintained in a hand-written parallel list, present for every sibling but one.
- wrong-error-source 〜 a guard or return consults the wrong error indicator: the wrong sentinel value, or errno where the callee reports by return.
- feature-gated-check 〜 a correctness check runs only when an unrelated optional feature flag is on.
- shift-ub 〜 1 << i shifts into the sign bit of int at the top of a legal index domain.
- recovery-desync 〜 a device-loss recovery restarts playback without resetting the state the restart invalidates.
- odd-sibling-out 〜 one of several sibling implementations omits a guard or ordering the others share.
- lookahead-off-by-one 〜 a paired window and delay length differ by one at a lookahead seam, so coverage expires one step before the guarded sample is consumed.
- unbounded-spin 〜 a loop or wait with no forward-progress or termination guard spins or hangs forever.
- dangling-capture 〜 a deferred record stores a caller pointer and dereferences it after the caller may have freed it, against a one-sided lifetime contract.
- size-mismatch 〜 a buffer is provisioned by the wrong metric (stored footprint vs rendered width) and the shortfall subtraction underflows.
- unguarded-delegation 〜 a header contract is delegated straight to a third-party allocator that does not honour it.
- truncating-cast 〜 a narrowing cast drops the high bits of a duration or size, silently changing the operation.
- retry-exhaustion 〜 a bounded retry loop's exhaustion arm is indistinguishable from success and consumes an out-param the failed call left unwritten.
- clock-not-reanchored 〜 a clock source is not re-based after a power transition (sleep or hibernate) that resets it, so the monotonic contract breaks and every delta held across the transition wraps.
- noop-not-honored 〜 a documented no-op input still changes the output when it lands in a boundary position (the last element) that the code consumes unconditionally.
- shared-mutable-state 〜 a function-scope static or global is reused across contexts that should be independent (concurrent engines, reentrant calls), so their accesses race or clobber each other.
- missed-repoint 〜 a resource is recreated (grown, reallocated) but one of the descriptor sets or references pointing at it is not updated, leaving a live binding to a destroyed handle.
- table-coverage-gap 〜 two generated lookup tables that must agree on coverage diverge, so some inputs fall through to a default path and behave inconsistently with their peers.
- alignment-contract-gap 〜 a type's declared alignment is weaker than the layout contract its own header documents, so nothing enforces the guarantee its callers and the GPU mirror rely on.
- partial-out-param 〜 a fallible call's failure arm writes some out-params and leaves others untouched, so the caller cannot tell what it now owns; the discharge-side twin of no-abort, named by the 2026-07-25 remediation.
- silent-drop 〜 an out-of-contract or unsatisfiable request is neither honored nor refused: it is discarded with no diagnostic and no failure channel, so the producer believes it took effect and the scene is simply wrong.

One status tag rides alongside the root causes rather than replacing them:

- pending-design-decision 〜 the defect is understood and its root cause is already tagged, but the repair is blocked on a contract choice with more than one defensible answer, so writing the fix would settle that question silently. Never the only tag on an entry. Historical carriers retired under docs/BUGS_DONE.md, "Settled open decisions". Status tags are excluded from every root-cause tag count. The tag currently marks nothing. Open-entry fix write-ups live under docs/BUG-HUNT.md, "Fixes for open entries", keyed by file:line only.


## Audio

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Collections

(anoptic_collections.h is an empty placeholder as of 2026-07-17 〜 no declarations, no src/ module; nothing to audit yet)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Filesystem

### Interface-level bugs and logic inefficiencies

### Implementation bugs

filesystem_win64.c:33 〜 ano_fs_gamepath rejects on the length of the WHOLE executable path before it trims the file name (:33 tests GetModuleFileNameA's raw return against MAXPATH, the trim runs after at :37-41), where both POSIX siblings trim first and test the directory they are about to copy (filesystem_linux.c:39-44, filesystem_macos.c:43-48) 〜 MAXPATH is 256 (anoptic_filesystem.h:16) and MAX_PATH is 260, so the whole band GetModuleFileNameA can legally report without truncating, 256 to 259 characters, is refused on win64 no matter how short the directory is, and win64 carries no post-trim check at all (:43-46 copies unguarded), so the guard is over-strict rather than merely misplaced and cannot simply be deleted 〜 the refusal is length 0, the contract's "unresolved" (anoptic_filesystem.h:32-33), and every consumer honours it into total feature loss: openEngineFile returns NULL on a zero-length dir (pipeline.c:30-32) so every SPIR-V load fails and not one pipeline is built (flat.c:87/:91/:99, compute.c:95-:463, additive.c:75/:77, shadow_pipe.c:60), ano_fs_logpath returns {0} (filesystem.c:48-50) so log_core.c:857-859 leaves g_outFile NULL and the session log degrades to console (:446-451), log_crash.c:34-40 drops to a bare <stamp>_CRASH.log in the launch CWD, text_raster.c:544-546 builds the font path off an all-zero str and disables the overlay, and ano_fs_chdir_gamepath (:71-72) returns false so main.c:1004-1006 leaves assets on the launch CWD 〜 reachable on any Windows install sitting four characters below the ANSI path ceiling, an ordinary deep install directory, and only there: the same directory depth boots on linux/macos 〜 no caller compensates, every one of them treats length 0 as unresolved and degrades 〜 logged 2026-07-26 〜 test: none (win64-only, needs a 256-259 character install path on a Windows host and no Windows runner exists)
- odd-sibling-out

### Interlink / Composition bugs 



## Log (including log_crash.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Math

(anoptic_math.h is types-only as of 2026-07-17 〜 mat4/Vector2/3/4 PODs, no ops, no src/ module; ops live in render/vertex.h. Composition audits belong to the render_bridge and vulkan_backend interlink edges.)

### Interface-level bugs and logic inefficiencies

(The former lead "anoptic_math.h:16 vs docs/math-conventions.md 〜 one of the two contracts is lying" is struck as of 2026-07-24: the doc was right and the header was wrong. `translate` writes the translation to `mat[3][0..2]`, `multiplyMat4` documents `temp[col][row] += a[k][row] * b[col][k]`, and `perspective` sets `matrix[2][3] = -1` 〜 all column-major, matching GLSL's mat4 so uploads need no transpose. The header comment now says so. No code changed.)

### Implementation bugs

### Interlink / Composition bugs 



## Memory

### Interface-level bugs and logic inefficiencies

### Implementation bugs

memory.c:9 〜 ano_heap_release destroys *in with no NULL guard while every LOCALHEAPATTR site in the tree carries a live mi_heap_new()==NULL arm inside the heap variable's own scope 〜 text_bake.c:530-532 returns ENOMEM, ano_GltfParser.c:197-202 logs and returns NULL, audio_mixer.c:703-705 returns false, ano_meshoptimizer.c:125-126 returns and :704-707 falls back to the identity copy 〜 so the cleanup attribute (anoptic_memory.h:41, contract at :37-38) fires ano_heap_release on the NULL the arm just refused on, and mi_heap_destroy asserts heap != NULL (mimalloc heap.c:389) four lines before its own NULL early-out (:393), with _mi_assert_fail calling abort() (mimalloc options.c:546) 〜 mi_assert is live at MI_DEBUG>0 (mimalloc internal.h:329-334) and mimalloc's CMakeLists.txt:315-319 sets MI_DEBUG=2 for every CMAKE_BUILD_TYPE=Debug, i.e. build.sh 2/4/5/6/7, so a refused scratch heap converts five deliberate graceful refusals into a process abort in every debug and test build while Release (MI_DEBUG=0) silently no-ops at heap.c:393 〜 nothing compensates: the cleanup runs after the return expression and no site can suppress it 〜 reachable only when the backing heap cannot serve sizeof(mi_heap_t) (mimalloc heap.c:247), exactly the exhaustion those arms were written for; remedy is one guard, `if (*in) mi_heap_destroy(*in);` 〜 the overflow-canary half of the lead this came from is refuted at source: no scoped-heap pointer escapes its scope at any of the five sites, so the wholesale reclaim loses no ownership guarantee, only MI_DEBUG's padding check, which is instrumentation coverage 〜 logged 2026-07-26 〜 test: pending
- unguarded-delegation
- seam-validation

### Interlink / Composition bugs 



## Mesh

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Music

### Interface-level bugs and logic inefficiencies

### Implementation bugs

music_host.c:45 〜 cadence_ok's lower bound is ANO_CADENCE_NONE, so the one AnoCadencePolicy value that is not an index into the [3]-wide cadence tables is the one value the guard admits: ano_music_set_override(e, "cadence_policy", -1.0) satisfies :238, installs the pin and stores -1 (:239), policy_of returns it verbatim (music_conductor.c:132-133) and gen_chord hands it to ano_next_chord as policy (music_conductor.c:576-577), where CADENCE_TARGET[-1] (music_harmony.c:119) and PRE_CADENCE_FUNCTION[-1] (:130) read one element before their [3] definitions (:41-42) 〜 the slot is chosen by position alone, CADENCE at pos == bars-1 and PRE_CADENCE at bars-2 (music_gen.c:100-103) over a phrase index ano_clock_position keeps non-negative (music_form.c:28-46), and neither shortcut above the call intercepts a cadence bar (the lament arm needs OPEN/FREE, the held arm FREE 〜 music_conductor.c:556-571), so the last two bars of every phrase read out of bounds for as long as the pin stands 〜 the value read becomes AnoChord.degree, a uint8_t (music_theory.h:74-87), survives plan_inversion untouched on a non-FREE slot (music_conductor.c:442-450) and is stored as prevChord (:584), so the next bar's ano_chord_function subscripts FUNCTION_OF_DEGREE, an [8] table, with degree up to 255 (music_theory.c:150/:161 via music_harmony.c:136) 〜 the same -1 arrives a second way through the config: expand degrades every out-of-contract authored cycle entry to ANO_CADENCE_NONE (:100-103) and policy_of subscripts that cycle with no sentinel test (music_conductor.c:146-147), so the 2026-07-25 wave-1 sanitizer manufactures the value it was written to exclude, while the only policy_of arm that does test the sentinel is the dramaturg one (:134-137) and ano_pick_cadence_policy can return 0..2 only (music_control.c:230-237) 〜 no in-tree caller pins cadence_policy, but ACMD_MUSIC_OVERRIDE forwards a game-supplied value verbatim (ano_synth.c:704-705), and anotest_music.c's domain walk pins 3.0 and -3.0, both correctly refused (:136, :144), leaving -1.0 the untested hole between them 〜 logged 2026-07-26 〜 test: pending
- seam-validation
- odd-sibling-out

music_host.c:232 〜 OV_REGISTER casts the raw override double to int and installs it with no domain check, while the two siblings six lines below validate before installing (:238 cadence, :242 mode) and the outward bridge declares the very domain the ingress never enforces, clamping registerCenter to 0..127 (music_ir.c:115-117) 〜 the pin flows to p.registerCenter (music_conductor.c:652-653) and becomes the melody window, lo/hi = registerCenter ± rangeSemitones (music_melody.c:707-708, twins at music_conductor.c:1068-1069 and :1227), which is the only bound on a placed pitch (mel_place anchors on registerCenter and clamps only into [lo,hi], music_melody.c:198-201), and mel_event writes the result as (uint8_t)p with no clamp (music_melody.c:382) 〜 so pinning "register_center" to 300.0 lands every melody note near 300 and truncates it mod 256 back into the legal band, sounding pitches the generator never chose, while 200.0 keeps 188..212 in-type and ano_synth_live_bar's total guard drops every event instead (ano_synth.c:429-431), silencing the melody layer with no diagnostic 〜 both violate AnoNoteEvent's documented MIDI 0..127 (anoptic_music.h:40-47) and both publish registerCenter 127 back to the caller (music_host.c:284 through music_ir.c:115-117), a value the generator never used, so the pinner cannot tell which happened; the config route reaches the same place with an in-type value, expand copying the public uint8_t through raw (:122) 〜 the accentDepth twin at :231 is defended downstream, apply_accent folding it into clamp_velocity's 1..127 (music_modifiers.c:139-147, :16-20), which is why only the register lane strands, and escalate clamps velocityCenter but not accentDepth (music_conductor.c:669-676) 〜 no in-tree caller pins either, main.c sending only ACMD_MUSIC_AFFECT (:894-900), but ACMD_MUSIC_OVERRIDE forwards a game-supplied value verbatim (ano_synth.c:704-705), the same ingress the tallied velocityCenter seam crossed 〜 logged 2026-07-26 〜 test: pending
- seam-validation
- odd-sibling-out

music_host.c:226 〜 OV_TEMPO stores the override double verbatim and nothing between it and the divisions it becomes ever tests it: mapped_params takes it as the bar-0 snap (music_conductor.c:597-598) and as the slew goal (:627), bypassing the [60,160] clamp ano_map_tempo applies to its own output (music_control.c:92-96, :38); ano_music_slew lands exactly on the goal once the gap falls under tempoSlewPerBeat (music_control.c:246-249, 2.0 per beat in the table the shipping engine runs 〜 :38 inherited by ano_mapping_table_electronic at :80, selected at main.c:328-329), p.tempoBpm rounds it (music_conductor.c:646), the bridge copies it unclamped (music_ir.c:105) and the host publishes it with the per-beat tempo points (music_host.c:284, :288-289) 〜 so ano_music_set_override(e, "tempo_bpm", 0.0) walks currentTempo down 2 bpm per beat to exactly 0.0, after which ano_synth_live_bar computes barSeconds = barQuarters * 60.0 / 0.0 = +inf every bar (ano_synth.c:423), clock_add stores a 0-bpm anchor that every later anchor divides by (:155-171, :166) and clock_time_at returns +inf (:180, :183), making the bar's frame stamp (uint64_t)(inf * sampleRate) an out-of-range double-to-integer conversion 〜 UB 〜 on that bar and every bar after it (:421-422); the +inf barSeconds also clears the positivity gate at :819 straight into ano_dsp_asr_init's attack and release (:823-824), and the offline twin divides identically (:318) 〜 a negative pin walks the same slew past 0 and stamps frames from times running backwards through the same conversion 〜 the omission is per-site rather than systemic, since ano_synth_score_begin already refuses barQuarters <= 0 (:192-193) and the delay lane already floors the identical divisor with fmaxf((float)p->tempoBpm, 30.0f) (:1115) 〜 the in-tree engine runs the mapper path this lives on (main.c:328) and pins nothing itself, sending only ACMD_MUSIC_AFFECT (:894-900), but ACMD_MUSIC_OVERRIDE forwards a game-supplied value verbatim (ano_synth.c:704-705) 〜 logged 2026-07-26 〜 test: pending
- seam-validation
- odd-sibling-out


### Interlink / Composition bugs 



## Render / Vulkan backend

### Interface-level bugs and logic inefficiencies

### Implementation bugs

shadow_resources.c:22 〜 createShadowResources has sixteen `return false` arms and not one discharges anything acquired above it: an arm at :43/:46/:47 leaves frame i's frustumBuffer created and bound plus every earlier frame's pair live; :69-:88 leaves the m==0 moment image, its GpuAllocation, its array view and up to ANO_SHADOW_ATLAS_LAYERS layer views live on top of all MAX_FRAMES_IN_FLIGHT frustum/sampleVP pairs; :148-:159 leaves both moment images and every view live; :165-:168 leaves the whole shadow rig plus the first slot_upload_create's device allocation live 〜 the caller treats false as "init failed" and unwinds through the module cleanup path, but only for state it can see, and the half-written frames[i].shadow and state->shadow* fields hold created-but-unregistered handles on every arm above their own assignment; boot-only, every arm a driver or host refusal, so nothing renders wrong 〜 it matters because ANO_FATAL/ANO_ERROR are diagnostic-only and an init failure is supposed to be a full unwind, which this function does not perform for its own acquisitions 〜 logged 2026-07-25 (rounds 2 and 4), left unforeclosed: the round-4 status fix at :102-104 touched a distinct defect and added no discharge 〜 test: pending 〜 the complete fix is one labelled unwind or acquisition ledger covering all sixteen arms, decided together with what the transient CB and the SlotUploads do on refusal
- ownership-leak

instance.c:192 〜 getRequiredExtensions mints one mi_strdup per extension name 〜 the GLFW loop at :192, the debug-utils name at :196 under DEBUG_BUILD, the portability name at :200 under __APPLE__ 〜 and returns only the mi_calloc'd pointer array (:187, :205), while its sole caller createInstance (:56) frees that array and nothing under it on both arms, the vkCreateInstance refusal at :85 and the success epilogue at :93, so every name block is unreachable and unfreed for the life of the process: two on a Linux release boot (glfwGetRequiredInstanceExtensions answers NULL or exactly 2, external/glfw/src/vulkan.c:242), three under either conditional, four on a Darwin debug boot 〜 the allocator domain is consistent, instance.c:12 includes anoptic_memory.h so strdup/calloc/free are mi_strdup/mi_calloc/mi_free (mimalloc-override.h:26/:22/:24), making this a plain leak and not a cross-allocator free 〜 nothing compensates: the array is the only handle any caller ever holds and getRequiredExtensions has exactly one call site, so the string pointers die at :93 〜 reached once on every boot through initVulkan (vulkanMaster.c:410) and once per run of the four registered Vulkan tests, and already inside the debug build's own accounting since main.c:1010-1011 enable mi_option_show_errors/show_stats 〜 the copies buy nothing: GLFW owns its array until glfwTerminate (glfw3.h:6347), the other two names are string literals, and ppEnabledExtensionNames (:59) is read only for the duration of vkCreateInstance (:81), so the repair is to drop the three strdups and keep the array 〜 logged 2026-07-26 〜 test: pending
- ownership-leak

ano_GltfParser.c:236 〜 parseGltf's skip arm publishes no refusal: prim_accessors answers false for any primitive lacking POSITION or lacking `indices` (:80), the arm ANO_WARNs and `continue`s (:235-:236), and the sole writer of geometryPoolIndex is the post-upload line the continue jumps over (:281), so the field keeps the zero it was carved with 〜 primPool comes out of the calloc'd asset block (:166, :175) through gltf_carve (:52-:61) 〜 and zero IS FALLBACK_MESH_INDEX (anoptic_render.h:122), the magenta/black cube slot 0 holds from boot (scene_buffers.c:425-:436), not ANO_RENDER_NO_MESH (anoptic_render.h:118) which the cull pass skips (anoptic_render.h:116-:117); the skipped primitive is still given a material row (:501, published :776) and still flattened (:843), and main.c:64 forwards descs[i].mesh_index verbatim into RCMD_CREATE, so it spawns as a fallback cube at that primitive's world transform instead of not spawning at all 〜 reachable on ordinary legal glTF 2.0, where `indices` is optional and cgltf_validate guards every index constraint behind `indices &&` and never demands POSITION (cgltf.h:1707-:1720), so a non-indexed primitive walks straight through the parser's own untrusted-input gate (:151); no in-tree caller compensates, main.c has no mesh_index filter, and the three shipped assets (render_api.c:137/:141/:146) are all fully indexed, so the wrong cube needs an asset the tree does not yet ship 〜 the sibling half of this seam is discharged and stays discharged: geometry_pool_upload_chain answers ANO_MESH_NONE in *out_lodBase on total refusal (geometry.c:548) and publishes a truncated chain's real length callee-side into meshes[lodBase].lodCount (geometry.c:546) after rolling meshCount back (geometry.c:545), so the unread return carries nothing the parser lacks and is not [[nodiscard]] (geometry.h:114) 〜 logged 2026-07-26 〜 test: pending
- silent-drop

### Interlink / Composition bugs 



## Strings (including strings_utf.h)

### Interface-level bugs and logic inefficiencies

### Implementation bugs

ano_strings_collate.c:504 〜 both allocation-failure sort fallbacks abandon the stability their own headers promise 〜 anostr_sort is "Stable (equal strings keep their order)" (anoptic_strings_utf.h:98) and anostr_sym_sort is "Stable." (:104), and the scratch path honours that at every stage (strict-> insertion :287-:288, stable radix scatter :301, tie_insertion :337, tie_view_cmp_'s trailing idx tie-break :368, tie_msd's allSame early-out leaving duplicates in input order :424-:428) 〜 when mi_malloc refuses the 32-byte-per-item record block the arms at :533-:537 and :599-:604 hand the caller's array to qsort instead: fb_sym_cmp_ (:520-:527) settles collation ties on the numeric symbol value, a total order that reorders them deterministically, so anostr_sym_sort(t, {t->count+1, t->count}, 2) 〜 out-of-range symbols reading as the empty string being sanctioned input (anoptic_strings_utf.h:106, ano_strings_intern.c:127-:128) 〜 comes back reversed under refusal and untouched without it, one call with two answers; collate_qsort (:504-:507) omits the tie-break entirely, returning anostr_collate alone, so equal elements move however qsort pleases, while the third sibling fb_order_cmp_ (:509-:516) keeps stability only because its element is the index itself 〜 anostr_collate is 0 exactly for byte-equal values (:127-:128, byte-order total at :134), so for anostr_sort the elements that move are byte-equal long strings with distinct backings, separable through the ptr variant (anoptic_strings.h:44-:51) 〜 reachable on any large sort under the memory pressure that makes mimalloc refuse (a million strings asks 32 MB of scratch) and deterministically at count > UINT32_MAX, which the same ternary routes to the same arm (:533, :599) 〜 nothing in-tree compensates: the sort API is public surface with no production caller, its only callers the suites (anotest_strings_sort.c, anotest_strings_fuzz.c, anotest_sortbench.c), none of which exercises a refusal 〜 logged 2026-07-26 〜 test: none (the arm needs an allocator-refusal seam; mi_malloc cannot be made to refuse from inside the process)
- odd-sibling-out

### Interlink / Composition bugs 



## Synth

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Text

### Interface-level bugs and logic inefficiencies

### Implementation bugs

text_gpos.c:294 〜 ano_gpos_extract_kerns truncates both of its working sets in silence: the kern-lookup list is capped at GPOS_MAX_LOOKUPS 16 by `!seen && kernLookupCount < GPOS_MAX_LOOKUPS` (:248, :294) and a lookup's subtable count clamped to GPOS_MAX_SUBS 32 (:249, :317-318), and neither drop logs nor reaches the return, so the function still answers 0 against its own contract "Malformed -> nonzero with dense possibly partial. 0 = success including 'no kerns'" (text_internal.h:89) and the sole production caller's error arm, the ANO_WARN at text_bake.c:477, is dead for every truncation 〜 the sibling cap in the same feature does both, warning at text_bake.c:446 and documented in the public header (anoptic_text.h:82), where these two are documented nowhere 〜 the sharp edge is that the lookup budget is spent before the type filter: :294 admits lookups in feature-list encounter order and only :315 skips the non-PairPos ones, so a face whose 'kern' feature fronts chained-context (type 8) lookups fills all sixteen slots with lookups the reader then discards and never reaches its PairPos lookups at all 〜 measured on shipping Google Noto faces rather than synthesised: NotoSansSyriac-Regular lists 26 distinct kern lookups under its DFLT default LangSys with its one type-2 PairPos at position 25, NotoSansSiddham-Regular lists 20 with both type-2 PairPos lookups at positions 19 and 20, so each extracts zero pairs, returns 0, and lands on bake_kerns' nz == 0 arm (text_bake.c:483) 〜 kerns NULL, kernCount 0, exactly the documented "nothing kerns" state (anoptic_text.h:74) 〜 for faces that do kern, and nothing downstream can tell the two apart since ano_text_kern answers 0 for absent and dropped alike (text_internal.h:62) 〜 2 of the 504 GPOS-bearing faces on a stock macOS install cross the lookup cap, the ingress is the public ano_text_font_load(path) (anoptic_text.h:35), and the reachable shape is the auxiliary-Noto-per-script pattern text_raster.c:562/:566 already runs for Runic and Greek, the three shipped fonts staying under it (Geist 2/3, NotoSans 3/2) 〜 the :317 subtable clamp is the same shape left unexercised by that survey, its only 64-subtable candidate riding a type-8 lookup :315 discards first 〜 no memory unsafety: kernLookups[16] and subs[32] stay bounded (:294, :317-318, :338) 〜 logged 2026-07-26 〜 test: pending
- silent-drop

### Interlink / Composition bugs 



## Threads

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Time

### Interface-level bugs and logic inefficiencies

### Implementation bugs

time_win64.c:402 〜 ano_sleep gates its coarse stage on `target_ns > ANO_SLEEP_SPIN_TAIL_NS` (:402, constant 1000000ULL at :352, target_ns = us*1000 at :398), so any request of us <= 1000 skips the waitable timer (:404-419) and the Sleep fallback (:423-430) outright and falls into the spin stage (:434-440), which is ano_busywait 〜 a bare ano_timestamp_raw() poll (:328-343), no syscall, no scheduler entry 〜 while anoptic_time.h:61 documents ano_sleep as "Sleep for us microseconds via OS facilities. Yields to the scheduler" and both siblings implement that: time_macos.c:163-168 spends one mach_wait_until on exactly this sub-window under the comment "a single kernel wait keeps the yield contract", and time_linux.c:116 calls clock_nanosleep unconditionally 〜 the comparison is strict, so us == 1000 spins too, and every in-tree sub-ms caller sits at exactly 1000: audio_mixer.c:667 paces the mixer's ring-full back-pressure arm at ANO_AUDIO_PACE_US = 1000u (:603) on a thread that lives for the process and enters that arm whenever the device queue is full, which is its normal steady state since the device drains one block per period while a render costs a fraction of one (:659-669), so on Windows the audio mixer thread hard-spins a core through healthy playback where it sleeps on macOS and Linux, and audio_mixer.c is a common TU compiled on every platform (src/audio/CMakeLists.txt:2-8); main.c:49, :163, :385, :691, :699, :707 and :715 are unbounded `while (!submit) ano_sleep(1000)` retry loops that on Windows spin against the very consumer thread they wait on 〜 no caller compensates, and audio_mixer.c:652-658 reasons about its arm as "the short wait wins back a slot" with the yield as the unstated premise 〜 the header is the contract: it is the only statement of the promise, and two of three platforms already keep it 〜 tests/anotest_time.c:158 already records the divergence ("sub-ms (spin-only on Windows)") without asserting against it 〜 logged 2026-07-26 〜 test: none (win64-only; the assertion is CPU time against wall time across ano_sleep(1000) and no Windows runner exists to see it fail first)
- odd-sibling-out

### Interlink / Composition bugs 



## UI

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Engine

### Interface-level bugs and logic inefficiencies

### Implementation bugs

### Interlink / Composition bugs 



## Leads for future passes

(unverified suspicions surfaced during census iterations 〜 chase, confirm, then move up into a tally or strike)

## Census (2026-07-18)

Post-merge tally of this file after splicing the attached census pass with the in-repo BUGS.md. Severity is inferred from the writeups (entries carry no severity tags). Leads are unverified and excluded from the tallied counts unless noted.

Amended 2026-07-24: the math-conventions lead was chased and split 〜 the row-major/column-major half was resolved and struck (the header comment was wrong, the doc right, no code change), and the alignment half was promoted to the tallied entry anoptic_math.h:21 (Latent). Net: tallied 69 -> 70, leads 42 -> 41, file items unchanged at 111.

### Rubric

| Level | Meaning |
|---|---|
| Critical | Memory corruption, crash/segfault, GPU device-lost / invalid-handle UB, or uninitialized use |
| Major | Wrong results, contract break, leak, race, or feature loss under plausible use |
| Latent | Explicitly latent / unreachable today / absurd-arg-only / exotic seam |

### Tally

| Severity | Count |
|---|---:|
| Critical | 32 |
| Major | 26 |
| Latent | 12 |
| **Tallied total** | **70** |
| Leads (unverified) | 41 |
| File items (tallied + leads) | 111 |

### Severity by section

| Section | Critical | Major | Latent | Total |
|---|---:|---:|---:|---:|
| Audio | 1 | 4 | 1 | 6 |
| Collections | 0 | 0 | 0 | 0 |
| Filesystem | 0 | 2 | 0 | 2 |
| Log | 2 | 0 | 0 | 2 |
| Math | 0 | 0 | 1 | 1 |
| Memory | 0 | 1 | 0 | 1 |
| Mesh | 1 | 1 | 0 | 2 |
| Music | 5 | 2 | 0 | 7 |
| Render / Vulkan | 18 | 10 | 2 | 30 |
| Strings | 0 | 1 | 1 | 2 |
| Synth | 2 | 1 | 0 | 3 |
| Text | 0 | 2 | 1 | 3 |
| Threads | 0 | 1 | 0 | 1 |
| Time | 0 | 1 | 4 | 5 |
| UI | 2 | 0 | 2 | 4 |
| Engine | 1 | 0 | 0 | 1 |
| **Total** | **32** | **26** | **12** | **70** |

Render holds 18/32 Criticals. Music is next (5). Time is almost all Latent.

### Remediation status (2026-07-24)

The `[X] Fixed` entries this section counts, and the one `wontfix`, are no longer in this file: they moved verbatim to `docs/BUGS_DONE.md` on 2026-07-25, grouped under the module headings they came from.

The one-off pass: entries whose fix is local to one function or one platform-sibling set, needing no API change and no new mechanism. 36 of the 70 tallied entries were `[X] Fixed` by it, and one more (`time_win64.c:310`) carries a `wontfix`. The severity tables above describe the census as found and are deliberately not rewritten.

A second pass on 2026-07-25 ran the other way, deleting thirteen guards that rejected unreachable inputs to prevent contained effects. Two of them, in `time_win64.c`, amend fixes this pass had landed the day before. The record is in BUGS_DONE.md, "Removed guards", together with the incidental findings the sweep turned up 〜 unverified, and not counted in the census above.

A third pass, later on 2026-07-25, verified those incidental findings and implemented the eleven determinations recorded in BUGS_DONE.md, "Remediation determinations": five landed structural (tier 1), six as single-seam invariants (tier 4), none as fault-site guards, each with type-level welds 〜 static_asserts binding table extents, cap relations, ABI masks and field types to the invariants the code now assumes. One census claim was refuted outright (the PC_NAMES index, under Engine seams), and five entries entered the census already fixed: delay.h, music_host.c motif n, ui_raster_ref.c tile entries, window.c monitor query, ano_render_bridge.c pose seam. It also settled the log_core.c:817 open decision and deleted the time module's entire clock-failure sentinel convention. Guard movement this pass: anoptic_logflood, anoptic_gltfguard and anoptic_boottelemetryguard closed; four new guards landed green (motifboundguard, dspdelayguard, uitileentryguard, cameraposeguard). The headless suite stood at four failures, all of them the remaining open decisions.

A fourth pass, later on 2026-07-25, settled the open decisions themselves and implemented all four the same day. The tier policy ranked answers the write-ups had treated as peers: the destroy-time ring drain landed owner-side in both render_bridge and audio (tier 1 completes the one asymmetric sibling among the drop paths; tier 2 rejects a type-erased release callback in the transport), the audio rides-home promise took its teardown clause outright since a two-phase drain exports a liveness obligation no tier can home, and the music ownership question dissolved at the verify gate 〜 the candidate table is call-transient, so thread_local scratch changes nothing observable, and music_theory.c's lazy bake died with it, replaced by rodata. The strings closure direction fell to measurement: the divergence set is empty, so the decomp trim drops dangling redirects, the case trim closes the same way (26 broken round-trip mappings now return identity), and assert_trim_closure fails generation before a file is written if either table reopens. One incidental entered the census already fixed 〜 the generator's padding-memcmp dedup, which made regeneration nondeterministic 〜 and one new open entry was filed (music_voicing.c:120, the voices ingress), then closed the same day: verifying that seam widened it to two sibling holes (a pcCount row overrun in the drop branch, a zero-pc fallback read), all three retired together. Guard movement: anotest_strguard, anotest_bridgeguard, anotest_audioshutguard and anotest_musicguard closed, anotest_voicingboundguard landed green; the headless suite is green for the first time, and TSan runs the music guard clean where it confirmed the race before.

A fifth pass, the remediation campaign of 2026-07-25, took the 27 that were left and ran four fix rounds against them. Round 1 fixed 19 of the 27 outright 〜 the 15 renderer entries plus the four platform singletons (`audio_win64.c:589`, `filesystem_win64.c:137`, `threads_macos.c:79`, `time_win64.c:148`) 〜 and deliberately fenced the seven resource-management entries, which stay open above with their guards red on purpose and their custody spans byte-identical after four rounds of work in the same files. The one `wontfix` was not touched. Rounds 2 to 4 then worked the strays the fixes surfaced, 44 items in all, of which 41 are source defects entering this tally already fixed and three were test-side (two new guards authored to pin invariants nothing read, one guard re-authored against an adjudicated ruling). Ten findings the campaign logged and did not fix are new open entries above. Seven contract questions were adjudicated rather than settled inside a fix; the record is in `docs/BUGS_DONE.md`, "Adjudicated contracts (2026-07-25)", and the arithmetic behind the table below is in `docs/BUG-HUNT.md`, "The 2026-07-25 remediation".

| Section | Fixed | Tallied |
|---|---:|---:|
| Audio | 12 | 12 |
| Filesystem | 2 | 2 |
| Log | 2 | 2 |
| Math | 1 | 1 |
| Memory | 1 | 1 |
| Mesh | 2 | 2 |
| Music | 9 | 9 |
| Render / Vulkan | 71 | 82 |
| Strings | 3 | 3 |
| Synth | 3 | 3 |
| Text | 3 | 3 |
| Threads | 1 | 1 |
| Time | 4 | 5 |
| UI | 5 | 5 |
| Engine | 1 | 1 |
| **Total** | **120** | **132** |

Rounds 5 and 6, later on 2026-07-25, reversed round 4's disposition on the six entries it had ledgered rather than fixed. The user's standing order is that an in-scope defect found during the campaign gets fixed, and those six were in scope: `audio_mixer.c:616`, `audio_win64.c:389`, `scene_buffers.c:43`, `texture.c:381`, `apply.c:66` and `apply.c:57`. Round 5 fixed all six plus the `backend.h` contract the light work had outrun; round 6 took the four strays round 5 left and fixed every one inside the round, so the stray flow that ran 21, 16, 11, 17 finally ran dry. Eleven fixes in all. Six were already tallied above as open entries, so they move Fixed only. Four enter the census already fixed and add a line each 〜 the `ANO_MESH_NONE` refusal sentinel with its fallback-mesh unwind, the `backend.h` static-region contract, the `anoptic_render.h` `castsShadow` static-lane documentation, and `refresh_static_shadow`'s declaration home. The eleventh is test-side (a stale comment in `anotest_staticrowdecodeguard` claiming an invariant stronger than any of its own CHECKs) and carries no tally line, the same rule that made 44 stray fixes move the board by 41. So Fixed rises 110 + 6 + 4 = 120, Tallied 128 + 4 = 132, and open falls 18 − 6 = 12. Per module: Audio 10|12 to 12|12, now fully remediated; Render / Vulkan 63|78 to 71|82. Every other module is unchanged.

What is left is no longer the systemic mass the census named, because the swoops were taken: Vulkan Result discipline and the ownership/rebind work retired the renderer block wholesale rather than file by file. Twelve entries stand open. Seven are the resource-management fence 〜 one custody question wearing seven anchors, and the campaign's own evidence for why it was fenced is that six independent rounds each stopped exactly at its boundary. Four are what the fixing turned up and could not close inside its own surface: the SPIR-V orphan family, the shadow-resources unwind and the texture post-acquisition arms (the same partial-construction shape as the fence), and the static frustum region's monotonic non-reclamation. The twelfth is the declined repair. No open entry is blocked on a contract choice any more.

A sixth wave, the trivial-fix pass of 2026-07-25, took the five open entries whose repairs were mechanically small and decision-free 〜 four fence anchors whose custody spans proved separable after all (apply.c:125, render_slots.c:92, swapchain.c:110, flat.c:244) and the SPIR-V orphan family (compute.c:80) 〜 and closed all five, one agent per entry, the family's discharge decision resolved as a local labelled unwind per builder, which the resource manager may later centralize without re-opening anything. The wave surfaced two strays and fixed both at source the same day: loadFile's non-total failure arms (pipeline.c:63, a new tallied entry entering fixed 〜 a dangling buffer pointer, and a free() one configuration change from crossing allocators), and the intra-batch duplicate-id hole beside the alloc_range rollback, closed table-side where the published prefix makes the check O(1) per element. The guards that had pinned the four fence anchors were removed days earlier in the black-box suite consolidation, red through the whole campaign; post-fix verification is review plus green suites, not a red-to-green flip. Fixed rises 120 + 5 + 1 = 126, Tallied 132 + 1 = 133, open falls 12 − 5 = 7: the three-anchor texture custody chain (texture.c:426, components.c:72, ano_GltfParser.c:277 〜 still one decision), the shadow-resources unwind, the texture post-acquisition arms, the static frustum churn, and the wontfix. Per module: Render / Vulkan 71|82 to 77|83.

Guard-test movement: 22 new guards authored and registered (3 in round 1, 14 across rounds 2 to 4, 5 across rounds 5 and 6), and all 15 renderer census guards flipped red to green with no assertion touched 〜 the fixes met the tests the census had already written. Two guard files needed link-stub retypes only, where a fix changed a stubbed signature (`recordCommandBuffer` becoming `[[nodiscard]] bool` broke `anotest_initdepthguard`'s void stub, and left `anotest_recordbeginguard`'s own call discarding a result); both are stubs and discards, no CHECK. Rounds 5 and 6 add `anotest_audiopaceguard` to the headless suite and `anotest_scenebindguard`, `anotest_texpixeldomainguard`, `anotest_staticcastdropguard` and `anotest_fallbackmeshguard` to the render one, each observed red against its own pre-fix TU and green after by direct link rather than by a suite run, so the totals move from 87 of 94 to 91 of 98 with the same seven fenced reds, and from 57 of 57 to 58 of 58, on the next build. Entries fixed without a runnable gate here are the four platform singletons (Win32-only, or Darwin-only for the barrier, which is TSan-verified on-host instead), `audio_win64.c:389`, and the ones the census itself marks `test: pending`.

A margin tally, 2026-07-26: the gotos.md sweep had routed four findings toward this file without entries. Two verified at source and enter above 〜 text_bake.c:507 and main.c:350, one open entry each for Text and Engine. One was already tallied 〜 the createTextureImage post-:504 arms sit inside the open texture.c:486 entry. And one was rejected on verification: the pickPhysicalDevice strand claim (device.c:345) 〜 the :481 arm frees the enumeration array itself, and availableDevices is ctx-owned custody reclaimed on every failure arm, since vulkanGarbage.ctx registers at vulkanMaster.c:416 before the :431 call, the failure arm runs unInitVulkan, and cleanupVulkan reaches its :252 discharge with calloc-zeroed slots freeing as no-ops 〜 the same caller-reclaimed shape the compute audit blessed for state-owned pipeline objects. Fixed unchanged at 126, Tallied 133 + 2 = 135, open 7 + 2 = 9.

A leads triage, 2026-07-26: eleven maximum-effort investigations settled all fifty leads in one pass 〜 the section is not a record, so the thirty-eight refuted are struck entirely rather than annotated. Twelve confirmed at source and enter above: filesystem_win64.c:33 (the pre-trim length gate), time_win64.c:402 (sub-millisecond ano_sleep is a pure busywait on win64 against its own yield contract), text_gpos.c:294 (silent GPOS truncation, the lookup budget spent before the type filter), ano_strings_collate.c:504 (allocation-refusal fallbacks abandon documented sort stability), memory.c:9 (ano_heap_release hands NULL to mimalloc's debug assert, converting five graceful refusals into aborts in every debug and test build), music_host.c:45/:232/:226 (the cadence sentinel admitted as an index, the register pin unclamped into uint8_t truncation or total melody drop, the tempo pin slewing into a division by zero), instance.c:192 (per-extension strdup leak), ano_GltfParser.c:236 (a skipped primitive spawns as the fallback cube instead of not spawning), ano_GltfParser.c:435 (one texture shared across colour and data slots uploads sRGB and corrupts every data read), and main.c:691 (the HUD submit spins never hear g_logicShouldStop and conflate alloc-refusal with backpressure). Tallied 135 + 12 = 147, open 9 + 12 = 21, fixed unchanged at 126.

A precedent wave, 2026-07-26: the three decision-free entries among the day's tallies closed, one agent each 〜 shadow_casters.c:151 (exact-footprint free lists; a second strand path, the budget-full rewrite discard, found and closed in scope; region-full unreachable while budgets hold), text_bake.c:507 (commit-last plus single discharge), main.c:350 (single label through a music_world_stop proven total; the :396 arm ruled full teardown). The shadow domain regained coverage 〜 the cited guard died in the 3047e1c cull, succeeded by tests/anotest_vk_shadow.c in the anotest_vk_* tier, falsified against HEAD 〜 and the render suite is 30 targets. Fixed 126 + 3 = 129, open 21 − 3 = 18, tallied 147.

A residue wave, 2026-07-26: the three entries whose fixes had been blocked on a product decision, all six anchors, closed against the work order the decisions produced. The texture custody chain (texture.c:426, texture.c:486, components.c:72, ano_GltfParser.c:277) and the colour/data split (ano_GltfParser.c:435) landed as one change, because the dual-view rewrite reshapes exactly the constructor outputs the custody work has to total; the submission results (main.c:691) landed separately. The split is keyed by glTF image, which the work order asked for and a first pass wrongly rendered as per-texture: a texture is an (image, sampler) pair, so keying construction by texture built one VkImage, one arena span and one mip chain per texture, and consolidated an image reached by both roles only when a single texture carried both 〜 not when two textures reached one image, which is the ordinary exporter shape the entry was written about. Images now carry the union of their textures' roles and are constructed once, and the two bindless indices are keyed by image as well, minted the moment the registry adopts it: however many textures reach an image, its colour interpretation is one slot and its data interpretation is one slot, which is what a bump-only array with no release makes load-bearing. A second pass reached for a per-texture sampler cache to justify keying the slots by texture; it was removed 〜 every descriptor names the engine's one textureSampler, and honouring tex->sampler is a separate feature, not part of closing this entry. A mixed image is built with one mip level: a blit chain filters in the base format's space, so any level below the top is exact through one view and wrong through the other. Two rulings in the work order proved wrong against the tree and were implemented as their intent rather than their letter: destroying the parser's views and image at the point of a refused adoption would invalidate the batch command buffer the copy and blits were already recorded into, so the discharge defers to the epilogue past the submit; and the constructors' acquisitions were reordered ahead of every vkCmd*, so no failure arm can destroy an image a borrowed CB references. Four strays surfaced and were fixed in scope, entering the census already fixed 〜 text_raster.c's discarded createImageShared result, which then created a view on a VK_NULL_HANDLE image and which only the new [[nodiscard]] exposed; spawn_scene's light-attach spin incrementing lid inside its retry condition, so every retry offered a different light_id; the bulk packer placing 16-aligned sub-arrays at unaligned offsets behind a 56-byte header and a count * 4 id run, undefined for three counts in four and diagnosable under UBSan; and ano_render_ui_set guarding the glyph pair while leaving its own five count/table pairs unchecked, the endpoint whose stated job is validating hand-built blocks. So Fixed rises 129 + 6 + 4 = 139, Tallied 147 + 4 = 151, open 18 − 6 = 12. Render / Vulkan takes five of the six retirements and three of the four strays; Engine takes the remaining one of each. Guard movement: five new targets 〜 anotest_texunwindguard, anotest_texregisterguard and anotest_gltftexleakguard succeed the three culled in 3047e1c, anotest_gltftexdomainguard and anotest_vk_texdomain are new 〜 plus anotest_render_bridge extended and anotest_rendersubmitguard added, and every one of them was falsified against a deliberately mutated source before being trusted. anotest_vk_texdomain is the only device-bearing member: an image copy applies no transfer function and vkCmdBlitImage takes an image rather than a view, so the only instrument that reads through a view is a sampler, and it dispatches a compute probe to get one. The suite is 36 of 36 on a scrubbed Debug build, the Release gate is clean, and the engine runs the real Vulkan path for 20 s under validation with zero VUIDs.

### Context

For a systematic audit census of a C23 + Vulkan + lock-free engine, ~70 tallied findings is in band; many Criticals are bad-input / OOM / rare-device paths the demo never crosses, not daily boot crashes. The point of tallying before whack-a-mole was to expose systemic gaps.

### Systemic gaps (fell swoops)

Local one-offs will remain (limiter window, mesh simplify, collation CE/decomp, Darwin barrier, TSC resume, measure_runs, voicing race). Most Critical mass 〜 especially Render + Music 〜 clusters into a few missing disciplines:

| Swoop | Rough blast radius |
|---|---|
| Vulkan Result + abort/unwind (FATAL aborts or tears down; no publish-on-failure; every fallible create checked) | ~12–15 Render Criticals |
| GPU resource → descriptor bind registry (recreate invalidates + rewrites all dependents before old handle destroy) | ~4–6 growth/UAF/rebind |
| Music/synth validate-at-ingress (create / set_override / config adopt + synth event upper bounds) | ~6–8 Music+Synth |
| Checked size arithmetic (`ano_mul_u32` / `ano_add_u32` or equivalent as the only cap/size path) | ~6–8 wrap guards |
| Ownership discharge on destroy/failure (adopted blocks, staging, pipeline temps) | ~6–8 leaks |
| Asset validate gate (`cgltf_validate` before accessor reads) | 1 + class |

Stacked: plausibly ~70–80% of Criticals and a large Major chunk 〜 not 90% of the whole file. Highest leverage order: Result discipline, bind registry, music/synth ingress, then arithmetic + ownership as house rules. Do not chase folder reshuffles or MI_OVERRIDE first; the gaps are seams and obligations.

### Three layers (do not conflate)

`backup-resource-manager` and the Vulkan Result swoop hit different layers. Conflating them leaves Render Criticals standing after a large merge.

| Layer | What backup-resource-manager aimed at | What the Criticals need |
|---|---|---|
| Asset / CPU resource mgr | Logical paths, rid registry, load-to-caller-heap, durable writes, bytes vs meaning | Hostile/truncated file loads, path chaos, size-then-read 〜 not VkBuffer UAF |
| Thread interconnects | `anoring_*` / `anoseqpub` / tickets in `anoptic_collections.h`; bridges migrate onto them | One ownership contract for adopted payloads; stop private twin rings |
| Vulkan Result + GPU bind graph | Essentially absent (GPU allocations stay renderer-owned per resource-manager-ownership.md) | FATAL fallthrough, discarded VkResult, growth/recreate without rebind |

Facts:

- Vulkan Result + abort/unwind does not depend on resourcemg. It is local `vulkan_backend` discipline. Resourcemg helps the load side (shaders via `ano_res_load`, kill `loadFile` antipatterns) but will not stop NULL shader modules riding into pipeline creates or depth init logging FATAL and continuing.
- The resource manager moves handles; the renderer owns GPU allocations. Resourcemg is not the descriptor/rebind swoop. That is a thin renderer-side table: device object live at (set, binding, frame); destroy ⇒ rebind or retire dependents.
- Rings: standardize, do not remake. `docs/URGENT-audiorace.md` already closed this 〜 `anoring_spsc` matches the audio/render bridge design; migration value is dedup + one destroy/drain contract. Land collections with the word-lane seqpub fix (branch `anoseqpub` still had the plain-memcpy race), migrate audio/render/log onto it, encode once: push = ownership transfer; destroy = drain + free `bulk_owned` / retired blocks.
- Practical order: (1) Vulkan Result + unwind and a small GPU bind/rebind registry; (2) promote collections rings with seqpub fixed and existing bridge ownership rules honored; (3) port resource manager (pools + `ano_res_load` + registry) for the asset/path class, with parsers still validating meaning.
