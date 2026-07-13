<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

Status: historical provenance and superseded planning material. The current resource-manager specification is `docs/resourcemanager-comprehensive.md`; authority, implementation-state, model-result, API-freeze, threading, save-policy, async-topology, and completion claims below are historical unless restated there.

# A User's Prompt
1. Remote-FS floor: 9P/SMB is a real deployment target; believe only bytes    
  read, never stat size/mtime, no file locks.                                   
  OK.                                                                           
                                                                                
  2. anoptic_filesystem.h stays the thin OS-path + append layer;                
  anoptic_resourcemg.h is the strict superset above it.                         
  OK. Though it should be anoptic_resources.h. And it's not a strict superset.  
  It's actually its own thing, with its own directory in src/. Also, do not go  
  and clobber every single usage of anoptic_filesystem around the engine. The   
  logger and the crash system are FINE as is. Resourcemgr is for resources      
  primarily, with saves and configs as a side effect of doing that well.        
  Logging is monolithic by design.                                              
                                                                                
  3. Logical paths only at call sites ("shaders/flat.frag.spv"); forward        
  slashes, relative, no .././empty segments; violations hit the failure         
  sentinel, never UB.                                                           
  Agreed.                                                                       
                                                                                
  4. Namespace = ordered roots: write root (ano_fs_userpath()) shadows          
  registered mounts (newest-first) shadows base mount (<gamepath>/resources);   
  first hit wins.                                                               
  OK.                                                                           
                                                                                
  5. Mounts carry an optional logical prefix ("models/" graft) from day one.    
  Yes. Strongly consider making use of our lovely anoptic_strings interning     
  functions. We have runtime and comptime interning.                            
                                                                                
  6. Identity is rid = FNV-1a-64 of the logical path, one key space with        
  ANOSTR_SID/anostr_hash.                                                       
  Sure. Again look at our interning string stuff.                               
                                                                                
                                                                                
  7. The resource manager never parses; cgltf/stb/FreeType/config keep meaning  
  but stop doing their own file I/O.                                            
  Incorrect. One of the resource manager's jobs IS parsing, I'm pretty sure     
  that's kind of the whole point. Right now we're doing it all over the place.  
  Handing useably binary data out of supported file types and resource handles  
  IS the point. This is very serious and merits a full reconsideration of how   
  you approach this. Now THIS is a candidate for strict superset-style headers. 
                                                                                
                                                                                
  8. Correctness machinery never deferred, performance machinery always         
  deferred; async built on sync, never the reverse.                             
  Sure.                                                                         
                                                                                
                                                                                
  v0 surface & architecture                                                     
                                                                                
  9. Eleven functions as sketched: init, mount, resolve, resolve_write,         
  subpath, exists (advisory only), load, write, quarantine, save_commit,        
  save_load.                                                                    
  For the core in anoptic_res.h, yes. You'll need to consider more and handle   
  everything that's scattered all over the renderer too, in                     
  anoptic_res_{agoodname}.h                                                     
                                                                                
                                                                                
  10. ano_res_load contract: fresh handle, fstat as hint only, read loop to     
  EOF, ANO_CACHE_LINE-aligned in the caller's heap, one guard NUL, NULL blob +  
  one log line on failure.                                                      
  Sure.                                                                         
                                                                                
  11. Heap-first parameter, caller's heap owns the bytes, no free function, no  
  module cleanup function; LOCALHEAPATTR scoped heaps as the idiom.             
  Hm. No. Again, the whole point of a resource manager is that it owns the      
  resources and provides handles to them that you can count on. Did you even    
  read the Game Engine Architecture book I provided? The resource manager       
  creates a mimalloc {appropriate type of allocator} for every major resource   
  category it handles and owns the heaps of. Of course these can be handed off  
  by copy or destructively to the Vulkan renderer for instance when they are    
  consumed. But parsing and skinning happens here, and it happens FAST. We also 
  consider more diverse types of allocators, for instance pooling allocators,   
  multipooling, stripe allocators.                                              
                                                                                
  Stopping the number-by-number scan here cause already I can see that you need 
  to redesign the entire system.                                                
                                                                                
  Materials you need to look at immediately and understand:                     
  Game Engine Programming (the book).                                           
  CppCon 2017: John Lakos “Local ('Arena') Memory Allocators, I added the       
  slides to docs/references/John_Lakos_Slides.pdf, read them.                   
  https://github.com/bloomberg/bde (grep arena / multipool).                    
                                                                                
  IN ESSENCE, the idea I've had in mind from the moment I read Game Engine      
  Architecture the first time (and I did read it cover to cover in 2023), was   
  immediately that the resource manager should be backed by a specialized       
  multipool allocator, with a smart handles system to everything it needs.      
                                                                                
  So this is actually a VERY seriously SOTA kind of system, and a               
  well-considered one at the pinnacle of years of engineering. And  because     
  these problems have already been solved by past art., as well as being        
  already solved in my head, we can just do it.                                 
                                                                                
  So what it would implicate:                                                   
  - A new strict superset of anoptic_memory.h called                            
  anoptic_memory_{agoodname}.h, implementing a variety of specialty allocation  
  strategies, namely the size-based multipool allocator over mimalloc.          
  - A resource manager core that handles the extremely fast gulping of files,   
  streaming, handle stuff you described, etc. But it ALSO is a place where      
  assets BELONG. Models, maps, textures, shadows, save-games, etc -- in essence 
  loadables as a whole, are placed in here and to various degrees of            
  transparency or graceful passing serve any and all modules that need files.   
  This is with various backing allocators, such as multipool allocators namely, 
  or in-kind (worldsaves together, loadable scripts together, gltfs together)   
  etc. You are to suggest at least 4 different competing hierarchy models that  
  will have to duke it out in the realm of ideas and implementation through     
  inspection and benchmarking when we actually do this, A B C D. Eg: A,         
  Multipool only. B. Of-a-kind grouped together, each further subdivided into   
  multipools for each kind. etc.                                                
  - A resource manager extension such as anoptic_res_graphics.h that actually   
  *does* handle parsing, skinning, and transparent handles or graceful passing  
  of ingested assets over to the modules that need them. We already use a gltf  
  parsing library and continue to do so, except now it'll an ano_...()          
  interface wrapper and be used internally in the guts of this module's .c      
  files where appropriate.      

# A Machine's Response

 What Lakos actually gives us

  His five dimensions — Density, Variation (of size), Locality, Utilization,
  Contention — are the axes that pick an allocator, and his 14 strategies reduce
  to three real tools plus one trick:

  - Monotonic (bump/arena): wins when utilization is high and lifetime is
  batch-shaped — allocate a storm, free it all at once. This is load-time
  staging.
  - Multipool (size-class pools, each an O(1) free list): wins for long-running
  subsystems with size variation and churn — steady-state streaming.
  Fragmentation-immune by construction.
  - Multipool<Monotonic>: multipool that replenishes its pools from a bump arena
  instead of the global heap — the composition, and often the overall winner in
  his AS11–14 rows.
  - Wink-out: skip per-object teardown entirely, drop the whole arena. His
  benchmarks show local allocators never lose and win up to 10×, and the
  indirection cost of reaching them through an abstract interface is noise. In
  our C23 world wink-out isn't magic, it's just mi_heap_destroy — we already
  believe this (it's the LOCALHEAPATTR thesis); the slides tell us to build the
  multipool and monotonic tiers on top of mimalloc heaps, not to stop at them.

  BDE's bdlma package is the production shape of this: MultipoolAllocator
  (geometric size classes, each pool chained),
  SequentialAllocator/BufferedSequentialAllocator (monotonic),
  ConcurrentMultipool for the shared case. All straightforward to realize in C
  over mi_heaps.

  The corrected architecture, in your terms

  Three deliverables, three headers:

  1. anoptic_memory_pools.h — strict superset of anoptic_memory.h (my name
  suggestion; alternatives anoptic_memory_arenas.h or _alloc.h, but "pools"
  covers multipool, chunk pool, and stripe honestly). Ships: ano_mem_multipool
  (size-class multipool over a backing mi_heap, wink-out via destroying the
  backing heap), ano_mem_monotonic (bump over a heap-owned slab, for load
  staging), ano_mem_pool (single fixed-size, the 512 KiB streaming chunk pool
  becomes an instance of this instead of a bespoke thing), and ano_mem_stripe
  (SoA-stride/cache-line striped placement) as the speculative fourth.
  Multipool-over-monotonic composition falls out if the backing is expressed as
  "any heap-like," which in our world is just: every allocator takes a parent
  mi_heap_t or a parent ano_mem_monotonic.
  2. anoptic_resources.h + src/resources/ — its own module, not a superset of
  filesystem, and filesystem's other clients (logger, crash) stay exactly as
  they are; monolithic logging is a feature. The core keeps everything that
  survived our earlier review — namespace/mounts/shadowing, remote-FS paranoia,
  durable writes, save generations, rid via ANOSTR_SID/anostr_hash, and mount
  prefixes and diagnostics names going through anostr_intern_t (comptime SIDs
  for literals, runtime interning for data-driven names). But its center of
  gravity flips: it owns category allocators and a registry, and its primary
  currency is the handle — {u32 slot, u32 gen}, the same shape as our tickets
  and the GEA §6.2 relocation-safe reference. Single-copy enforcement lives here
  (rid → slot map, mutex-guarded per the rings-for-queues/mutex-for-maps rule).
  The raw byte-gulp with a caller heap doesn't die — it demotes to the
  internal/low-tier primitive the owner uses (and the escape hatch for genuinely
  unowned reads like config bootstrapping).
  3. anoptic_res_graphics.h (first extension; _audio, _world later) — where
  parsing genuinely lives. cgltf keeps doing the glTF understanding but
  disappears into src/resources/graphics/*.c behind ano_res_* calls; stb_image
  likewise. It ingests, parses, skins, and conditions into the category
  allocators, then serves consumers by handle, by copy, or by destructive
  hand-off (the Vulkan staging case — ownership transfer, which our render
  bridge already knows how to phrase). The bake path (step 7's load-in-place)
  becomes this module's fast path rather than a bolt-on: baked = the parse
  already happened offline, ingest degenerates to gulp + fix-up into the same
  allocator, same handle out.

  The four competing hierarchy models

  All four agree on the outer facts (rid identity, handles out, wink-out
  teardown); they disagree on what a heap is. Each is buildable behind the same
  anoptic_resources.h surface, so they can genuinely duke it out in
  anotest_resbench:

  A — One multipool to rule them. A single resource multipool over one mi_heap;
  category and lifetime are tags in the registry, not allocator boundaries.
  Purest Lakos AS9/AS10. Best density and simplest code; teardown of a subset
  (one level's assets) is per-object, not wink-out, which is its likely losing
  wound. Its bench: steady-state streaming churn, where size-class recycling
  shines.

  B — Kind-major. One arena per resource category — models, textures, shaders,
  levels, saves/config, scripts — each backed by its own multipool sized to that
  kind's size histogram (texel payloads have wildly different V than glTF node
  metadata). Of-a-kind locality: a scan over all loaded models touches one
  region. Teardown per kind is wink-out. Its wound: lifetime crosses kinds (a
  level's model dies with the level, not with "models"). Its bench:
  whole-category iteration and per-kind fragmentation over a long soak.

  C — Lifetime-major. Arenas by lifetime — engine-forever, per-level,
  streaming/transient — each subdivided into multipools (per-kind or per-size
  inside). This is Hydro Thunder's double-ended stack grown up, and GEA §7.2.2's
  stack-based resource memory. Unload-the-level = destroy one heap, done; the
  handles system marks those generations dead. Its wound: shared-across-levels
  resources need either promotion to engine-lifetime or the refcount machinery
  we rejected. Its bench: level load/unload cycles — wall time and residual
  fragmentation after 50 cycles.

  D — Role-split (metadata/payload). Two tiers regardless of kind: small hot
  metadata (registry rows, handles, names, glTF node/material structs) in one
  shared multipool; bulk payloads (texel data, vertex/index blobs, save
  payloads) in dedicated chunk-pool/stripe allocators with big fixed blocks —
  the 512 KiB pool generalized. Effectively Multipool<Monotonic> with the
  monotonic side being payload slabs. Best fit for the streaming economy and GPU
  hand-off (payloads are already in staging-shaped blocks); its wound is that a
  "resource" spans two allocators, so destructive hand-off and wink-out need
  per-resource bookkeeping. Its bench: streaming throughput into Vulkan staging,
  zero-copy hand-off rate.

  E (the one I'd bet on, stated for honesty) — C×D hybrid: lifetime-major arenas
  whose insides are role-split — per-level heap containing a metadata multipool
  + payload chunk pools, engine-lifetime ditto, transient monotonic for parse
  staging that winks out at end-of-ingest. That's three Lakos tools each doing
  the one job they win at. It has to earn that on the bench like the others.

  Consequences for the plan of record

  Rules 5 (interning), 7 (never parses → core never parses, extensions exist to
  parse), 9 (core + extension headers), and 11 (ownership inversion: category
  allocators + handles, caller-heap demoted to internal primitive) get
  rewritten. The rejections table loses "handles + generations (now)" — handles
  are in from the start; what stays rejected is refcounting until the
  cross-level-sharing question forces model C to answer it. Naming:
  anoptic_resources.h, src/resources/, anoptic_res_graphics.h,
  anoptic_memory_pools.h — all open to your better names. Everything from the
  durability/remote-FS half of the plan survives untouched; it was the ownership
  half that was wrong.

  So yes — I understand, and I think this is the stronger system: the earlier
  plan optimized for "the resource manager stays out of the way," yours
  optimizes for "the resource manager is where assets live," which is what makes
  fast parsing, single-copy, streaming, and GPU hand-off one coherent thing
  instead of four. When you're back with your point-by-point checklist thoughts,
  I'll fold them and this redesign into a rewritten resource-manager-unified.md
  (new name probably warranted too — it's a memory-and-resources plan now),
  with the A–D(E) bake-off written in as a first-class step with its own bench
  bar.

# The elegance of strings

  Note how anoptic_strings.h and anoptic_strings_utf.h are actually what I      
  consider to be a rather excellent example of handles to data being handled    
  gracefully. A string under a certain size degrades to a normal stack array,   
  past that it becomes a real utf string. It can be sliced and diced and        
  spliced however you want. You can reclaim ownership of it. You can intern it. 
  That entire module was designed explicitly as an example of how arbitrary     
  lenghts of byte data should be considered. Guess what else is an arbitrary    
  length of byte data?                                                          

● Bytes from a file. Yes.

  The strings module isn't just a utility I should borrow converters from — it's
  the design brief. anostr_t is a worked answer to "how do you hand out
  arbitrary-length byte data": a 16-byte value you pass by copy, that internally
  is either the data itself (small) or a handle to the data (large), where the
  consumer never branches on which; views that slice without copying or owning;
  a builder for assembly; explicit ownership reclaim when you need the raw
  buffer back; and an intern table that turns "the same bytes twice" into "the
  same small integer twice," with dense symbols for side arrays. Every one of
  those has a direct resource-manager translation:

  - The value type. A resource reference should be a small value with the same
  dual nature — trivially copyable, meaningful to compare, no branching at the
  consumer. The German-string trick even carries over semantically: the inline
  prefix that makes anostr_t comparisons fast without chasing the pointer is,
  for a resource handle, the cached rid/generation that makes validity checks
  and lookups fast without touching the registry.
  - Slices are views. A mesh range inside a glTF binary buffer, one mip inside a
  texture payload, one entry inside a pack — these are anostr_view in different
  clothes: non-owning windows into an owned allocation, cheap to mint, dying
  before or with their backing. That's the answer to how anoptic_res_graphics.h
  serves consumers "with various degrees of transparency": mostly it hands out
  views into memory the manager owns, and only copies or destructively transfers
  at real ownership boundaries (Vulkan staging).
  - The intern table IS the registry. Single-copy enforcement, rid → dense slot,
  side arrays indexed by symbol — anostr_intern_t already is the shape I
  described for the step-4 registry, and you built it as the worked example on
  purpose. The resource registry is that structure generalized from "unique byte
  strings" to "unique loaded payloads," with generations added because
  resources, unlike interned strings, can die.
  - Ownership reclaim. anostr lets you take the buffer back out of the value;
  the resource analog is the destructive hand-off — the manager relinquishes a
  payload to the renderer and the handle's generation retires. Same verb.
  - Small degrades to inline. Even this may translate: a 40-byte config value or
  a tiny shader doesn't need a slot in a payload pool any more than "hello"
  needs a heap allocation. Whether resources get true SSO or just a small-object
  class in the multipool is exactly the kind of question the A–D bake-off
  answers — but the principle, size-dependent representation behind a
  size-independent interface, comes straight from the string.

  So the design lineage is: anostr_t shows the value/view/intern/reclaim grammar
  at the scale of one allocation; anoptic_resources.h speaks the same grammar
  at the scale of the whole asset population, with the multipool tier from
  anoptic_memory_pools.h playing the role mimalloc plays for strings. The
  resource blob probably shouldn't be {void*, size_t} at all — it should be an
  anores_t-style value with view/owned duality and the registry behind it, so
  the API reads like the strings API and a developer who's learned one has
  learned both.

  That's the coherence I was missing when I wrote "caller heap, no free
  function, librarian hands you a buffer."

# The Resource Manager, For Real

Status of this section: superseded plan preserved in place. Its 2026-07-12 audit, permanent mutex doctrine, reduced A-versus-E result, frozen V1 surface, save-retention text, logger-port transport, and former done bar are not current specification; see `docs/resourcemanager-comprehensive.md`.

**Status:** the single plan of record. Supersedes `resource-manager-unified.md` and everything it superseded. What we code, we ship: every step lands whole, tested, and permanent — no placeholders, no throwaway tier. Deferrals exist only as bench-gated rungs, never as "correct design later."
**The correction this plan encodes:** the previous plan designed a librarian — finds bytes, hands them to whoever asked, owns nothing, parses nothing. A resource manager is an **owner**: the place where loadables live, backed by purpose-built allocators, handing out handles you can count on, doing the parse/skin/condition work inside its own walls and passing finished, usable data to the modules that need it. Gregory ch. 7 §7.2, not §7.1.
**House premise:** the hard parts are in-tree. The logger is the async transport (lock-free MPSC ring, owned drain thread, 22–48 ns enqueue, TSan-clean, fuzz-oracled). The render bridge ships SPSC ownership transfer and false-on-full tickets. And `anoptic_strings.h` is the **design brief**: a 16-byte value that is either the data itself or a handle to it, views that slice without owning, an intern table that turns same-bytes into same-integer, explicit ownership reclaim. A resource is an arbitrary length of byte data. The resource manager speaks the string module's grammar at the scale of the whole asset population.

---

## 1. Founding rules

1. **Remote-FS floor.** 9P/SMB is a real deployment target (WSL2 dev loop, roaming `%APPDATA%`). Believe only bytes you have read: never trust stat size or mtime, never depend on file locks, validate content by framing and hashes.
2. **The manager owns resources.** Models, maps, textures, shaders, saves, scripts — loadables as a whole live in manager-owned allocators. Consumers get handles, views, copies, or destructive hand-offs; they do not get to own the population.
3. **One name.** Call sites use logical paths (`"shaders/flat.frag.spv"`): forward slashes, relative, no leading `/`, no empty/`.`/`..` segments, no backslashes, `root + '/' + path` fits `MAXPATH - 1`, compiled literals fit `ANOSTR_SID_MAX` (128). Violations hit the failure sentinel, never UB.
4. **Identity is an integer.** rid = FNV-1a-64 of the logical path: `ANOSTR_SID` compiled, `anostr_hash` runtime, one key space, bridge `ANOSTR_SID(x) == anostr_hash(anostr_lit(x))`. Mount prefixes, diagnostics names, and data-driven identifiers ride `anostr_intern_t` — comptime SIDs for literals, runtime interning for names read from data files.
5. **Parsing is the job — in the extensions.** The core moves bytes and never interprets them. The per-domain extensions (`anoptic_res_graphics.h` first) exist precisely to parse, skin, and condition: cgltf, stb_image, FreeType keep doing the understanding but disappear into `src/resources/` behind `ano_res_*` calls. No other module opens or parses asset files.
6. **Writes are durable or refused.** Old-complete or new-complete on disk, never torn.
7. **Correctness never deferred, performance always deferred.** The full write protocol ships with the write path; io_uring may never ship. But "deferred" means a recorded rung with a bench bar, never a stub.
8. **Async on sync, never the reverse.** Sync load is a pure function of (frozen mount table, logical path, destination allocator); the async tier is the logger's transport ported, not a redesign.
9. **Stay in your lane.** `anoptic_filesystem.h` remains the thin OS-path + append layer and keeps its existing clients: the logger and the crash system are monolithic by design and are not migrated. The resource manager is for resources; saves and configs fall out of doing that well.

## 2. Current state (audit, 2026-07-12)

| Site | What | Path mechanism today |
|---|---|---|
| `pipeline.c` `loadFile`/`openEngineFile` (~20 sites: `pipeline.c`, `flat.c`, `transmission.c`, `additive.c`) | shaders | gamepath-relative `"resources/shaders/X.spv"` interim shim; size-then-read and `free()`-vs-`ano_aligned_free` bugs live inside `loadFile` |
| `vulkanMaster.c` `parseGltf` sites | models | CWD-relative into gitignored `assets/`, behind the chdir shim at `main.c:588` |
| `ano_GltfParser.c` → `texture.c` `stbi_load` | textures | raw glTF image URI, CWD-relative |
| `text_raster.c` | fonts | hand-rolled gamepath join |
| logger | append stream | `ano_fs_logpath()` = `<gamepath>/logs`, `<stamp>_ano.log` — stays as is |

In-tree assets: `resources/` exists (shaders, fonts, textures) and stages next to the exe; models still sit in `assets/`. `anoptic_collections.h` is an empty stub awaiting the ring port. `anoptic_strings.h` ships `ANOSTR_SID`/`anostr_hash`, the intern table, split/join/builder, and UTF-16 boundary converters. `anoptic_filesystem.h` has `ano_fs_gamepath/userpath/logpath/session_stamp` and the append family; no read-open, stat, enumeration, atomic replace, or mkdir -p — that OS surface is new work inside `src/resources/`. Identity today is `char name[64]` in `ModelAsset` plus three hardcoded `parseGltf` filenames. Parsing today happens all over the renderer; this plan pulls all of it into one module.

## 3. The memory tier: `anoptic_memory_pools.h`

A strict superset of `anoptic_memory.h`: specialty local allocators over mimalloc, in Lakos's vocabulary. His five dimensions — **D**ensity, **V**ariation, **L**ocality, **U**tilization, **C**ontention — pick the tool; his benchmarks (CppCon 2017, `docs/references/John_Lakos_Slides.pdf`) show local allocators never lose and win up to 10×, and that reaching them through an indirect interface costs nothing measurable. Production shape: Bloomberg `bdlma` (`MultipoolAllocator`, `SequentialAllocator`, `ConcurrentMultipool`).

Four allocators, one convention: every allocator takes a parent (`mi_heap_t` or another pools allocator), so composition (Lakos's `Multipool<Monotonic>`, his overall benchmark winner) falls out of the constructor rather than a template.

- **`ano_mem_multipool`** — geometric size classes, each class an O(1) free list carved from the parent in slabs. For long-running subsystems with size variation and churn: steady-state streaming. Fragmentation-immune by construction.
- **`ano_mem_monotonic`** — bump allocator over parent-owned slabs, reset or destroy, no per-object free. For batch-shaped lifetimes: parse staging, level load. High-U short-lived storms.
- **`ano_mem_pool`** — single fixed block size, free list, false-on-empty. The 512 KiB streaming chunk pool is an instance of this, not a bespoke thing.
- **`ano_mem_stripe`** — cache-line-striped placement for SoA side arrays. The speculative fourth; ships only with a consumer and a bench.

**Wink-out is not magic here, it is `mi_heap_destroy`.** Every pools allocator is backed by exactly one mi_heap; destroying the heap reclaims every slab of every pool inside it in one call. Group = one heap, teardown = bulk free — the existing `LOCALHEAPATTR` thesis, extended one tier up. Per-allocator high-water marks and slab counts are queryable (the §10.9 lesson: instrumentation must live inside each allocator); stats fold into the FATAL crash path.

Threading: allocators are single-owner by default (the `anostr_intern` discipline). The shared cases in this plan are structural, not locked: the IO thread and workers own their own allocators, ownership of filled memory transfers through completion messages.

## 4. The value grammar: `anores_t`

The string module is the worked example of handling arbitrary-length byte data; the resource manager reuses its grammar wholesale.

- **The handle is a 16-byte value**, `anores_t = {u64 rid, u32 slot, u32 gen}`, passed by copy, trivially comparable. The inline rid is the anostr prefix trick: identity checks and registry lookups without chasing a pointer. `{0,0,0}` is the failure sentinel. A retired generation makes every outstanding copy of the handle politely invalid — lookups return the sentinel view, never UB.
- **Views slice without owning.** `ano_res_bytes(h)` yields an `anostr_t`-shaped byte view of the whole payload; domain accessors yield views of ranges inside it (a mesh's vertex range inside a glTF buffer, one mip inside a texture, one entry inside a pack). Views are valid until the handle's generation retires; the debug build asserts on stale access.
- **The registry is the intern table generalized.** rid → dense slot, single-copy enforcement (loading the same logical path twice yields the same handle), generation counter per slot, side arrays indexed by slot (payload pointer, size, category, allocator id, state). Open addressing over cached hashes, the `anostr_intern_t` shape, guarded by a mutex — rings for queues, mutex for maps. Debug asserts on rid collision.
- **Ownership reclaim is a verb.** `ano_res_release(h)` destructively hands the payload out of the manager (the Vulkan staging case: the render bridge takes the block, the generation retires). Copies are explicit; hand-offs are explicit; everything else is a view.
- **Size-dependent representation behind a size-independent interface.** Whether tiny resources (a 40-byte config value) get inline storage in the registry row or just a small size class in the multipool is decided by the hierarchy bake-off (§5), not by the API — the handle looks identical either way.

## 5. The hierarchy bake-off

The interface freezes; what a heap *is* does not. At least four models compete, all buildable behind the same `anoptic_resources.h` surface, decided by inspection and `anotest_resbench` when the real asset load exists (step 3). Each model states its predicted wound honestly; the bench looks there first.

- **A — one multipool.** A single resource multipool over one mi_heap; category and lifetime are registry tags, not allocator boundaries. Best density, simplest code. Wound: unloading a subset (one level) is per-object frees, not wink-out. Bench: steady-state streaming churn.
- **B — kind-major.** One arena per category (models, textures, shaders, levels, saves/config, scripts), each a multipool tuned to that kind's size histogram. Of-a-kind locality: scanning all loaded models touches one region; per-kind teardown is wink-out. Wound: lifetime crosses kinds — a level's model dies with the level, not with "models". Bench: whole-category iteration, per-kind fragmentation over a soak.
- **C — lifetime-major.** Arenas by lifetime (engine-forever, per-level, streaming/transient), multipools inside. Hydro Thunder's split grown up; unload-the-level = destroy one heap, handles retire by generation sweep. Wound: resources shared across levels need promotion or the refcounting we rejected. Bench: 50 level load/unload cycles, wall time and residual footprint.
- **D — role-split.** Metadata (registry rows, names, node/material structs) in one shared multipool; bulk payloads (texels, vertex blobs, save payloads) in big-block `ano_mem_pool`/stripe allocators. Payloads arrive staging-shaped for GPU hand-off. Wound: one resource spans two allocators, so release and wink-out need per-resource bookkeeping. Bench: streaming throughput into Vulkan staging, zero-copy hand-off rate.
- **E — the favored hybrid, stated for honesty: C×D.** Lifetime-major arenas whose insides are role-split, plus a transient monotonic for parse staging that winks out at end-of-ingest. Three Lakos tools each doing the one job they win at. It earns the slot on the bench like the others or it doesn't ship.

**Bar:** the winner beats every loser on its own home bench or shows why the loss doesn't matter at our shapes; the decision and numbers are recorded in this document.

**Result (2026-07-12, `anotest_resbench` at -O3, Linux ext4 primary + Windows native cross-check): E ships.** A vs E, best of two: churn is a structural tie (E outside a scope IS model A — group 0 — so the loss cannot exist; measured identical, hit p50 50 ns both); E wins its home bench outright (residual footprint 0 vs A's 1,318,912 bytes of high-water chunks retained every one of 50 level cycles, cycle wall 6.49 vs 6.79 ms mean, p99 8.1 vs 12.9 ms); hand-off ties (200/200 zero-copy both, release p50 30 ns). E-v1 retire is chunk-granular multipool destroy (mi heaps are single-thread-owner; true heap wink-out arrives with the step-5 loader thread — the upgrade changes only the parent constructor at scope-begin). A stays behind `ANO_RES_MODEL=A` as the bench baseline. Full grid and decisions: RESOURCE_MANAGER_IMPL.md Phase D.

## 6. Module layout

- **`include/anoptic_memory_pools.h`** + `src/memory/pools.c` — the allocators of §3. Platform-free (mimalloc is already the platform layer).
- **`include/anoptic_resources.h`** + `src/resources/` — its own module, not a superset of filesystem. Core: namespace and mounts, registry and handles, sync read path, durable write path, saves. `src/resources/resources_core.c` platform-free; per-platform TUs behind internal `resources_os.h` (`rmos_exists`, `rmos_read_all`, `rmos_mkdir_p`, `rmos_open_temp_excl`, `rmos_write_all/sync/close`, `rmos_rename_replace`, `rmos_sync_dir`, `rmos_scan_dir`), POSIX half shared Linux/macOS, reads in ≤ 512 KiB chunks from day one. Path internals ride anostr (`anostr_view`, `anostr_split`, builder); `ano_fspath` materializes only at the OS boundary; Windows wide strings via `anostr_to_utf16`.
- **`include/anoptic_res_graphics.h`** + `src/resources/graphics/` — the first parsing extension: glTF ingest (cgltf internal), image decode (stb_image internal), font blobs for FreeType, shader blobs for Vulkan. Ingests into manager allocators, serves views/handles, destructively hands staging blocks to the render bridge. Later siblings when a real consumer exists: `anoptic_res_audio.h`, `anoptic_res_world.h`.

## 7. The namespace

A static table of absolute roots, written once at init/mount on the main thread, read-only forever, lock-free resolution, debug atomic `ready` flag. First root containing the file answers.

1. **Write root** — `ano_fs_userpath()`. Every write lands here; it shadows every read (user overrides, saves, mods, loose-over-pack hot reload — one mechanism).
2. **Registered mounts**, newest-first. Dev build registers the source tree's `resources/` in `main()` via `ANO_DEV_RESOURCES`, consumed at that one site only. Mounts carry an optional logical prefix as an interned `anostr_sym` (`"models/"` grafts `assets/` during migration); the field exists from day one.
3. **Base mount** — `<gamepath>/resources`. Installing = the exe with `resources/` next to it.

A pack (step 7) is just another mount in the same walk. The logger's append stream under `ano_fs_logpath()` stays where it is — logging is monolithic by design and outside this namespace.

## 8. Performance model

- The fight is request rate, not bandwidth: NVMe saturates on queue depth. Ceiling targets adopted from DirectStorage: 50K req/s at ≤10% of one core, 2 GB/s sustained.
- Two IO shapes: bulk level load (easy) and steady-state streaming of small ranged reads (the hard shape everything serves). One CPU shape joins them: **parse fast** — ingest into a monotonic staging arena, condition into the owning allocator, wink the staging out. Zero loose `malloc`/`free` anywhere in an ingest.
- Default path is buffered and page-cache-friendly; the pack tier keeps the cold-start door open. Never trade the warm dev loop for a cold-start benchmark.
- Transport is already free (22–48 ns enqueue vs ~10–100 µs per NVMe op); the budget is queue depth, decompression, memory placement — and placement is now a first-class design axis (§5), not an afterthought.
- Compression is bandwidth amplification, proven at step 6 or reverted. Prefetch is disclosure: the level file names its assets, the IO thread never guesses.
- Backend ladder, one completion-shaped interface: rung 0 one IO thread + blocking `pread` + `posix_fadvise`; rung 1 2–4 threads; rung 2 io_uring/IOCP. Each rung must beat the previous in `anotest_resbench` percentiles to merge.

## 9. Frozen formats

**Save frame v1.** Little-endian, 48-byte header: magic `'ANOS'` (4), container_version=1 (2), hash_id (1; 1=FNV-1a-64, 2 reserved xxh3-64), flags=0 (1), format_version (4), min_reader_version (4), payload_len (8), seq (8, echoed from the filename), header_hash (8, FNV-1a-64 over bytes 0–31), reserved (8); payload; 16-byte footer: payload_hash + `'ANOSDONE'`. Truncation caught three independent ways; header vs body corruption distinguishable.

**Pack TOC (anopak).** Header `{magic 'ANOPAK\0\1', u32 entry_count, u64 toc_offset}`; sorted flat TOC of `{u64 rid, u64 offset, u64 size, u64 csize, u8 codec, u8 hash_id, u16 flags, u64 payload_hash}` binary-searched by rid; payloads 4 KiB-aligned; TOC checksummed, verified at mount — a corrupt pack refuses at startup. Codec byte reserves GDeflate's id.

**Handle.** `anores_t {u64 rid, u32 slot, u32 gen}`; `{0,0,0}` sentinel. **Ticket.** `{u32 idx, u32 gen}`; `{0,0}` on a full ring. Request payload `{rid or path, offset, length, band, ticket}` — ranges in the format from day one.

**Load-in-place aliasing policy.** Baked images are PODS; pointers are offsets fixed up in one loop at load. Reinterpretation happens via `memcpy` or union punning (both defined in C23), never bare pointer casts — a bare-cast pun is a latent miscompile at high `-O`.

## 10. Write protocol

POSIX: same-dir `O_EXCL` unique temp → write all (loop short/EINTR) → `fsync(fd)` → close → `rename` → parent dir `fsync`. Any write/fsync failure: unlink temp, return -1, never re-fsync the same fd (fsyncgate); a retry is a fresh protocol run. Dir-fsync failure after a successful rename logs loudly, returns 0.

Windows: `CreateFileW` temp share-mode 0, `WriteFile` loop, `FlushFileBuffers`, `CloseHandle`; `ReplaceFileW` when the target exists else `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`; both retried 5× with 100 ms backoff on `ERROR_SHARING_VIOLATION`.

**Saves.** Every generation is a brand-new filename `saves/<slot>.<seq>.anosave` (a name that never existed has no stale cache entry), framed per §9, re-opened fresh and re-validated. Saves are user data and the engine NEVER deletes them on its own (revised 2026-07-12; `ANO_RES_SAVE_KEEP` survives as the advisory bulk threshold): `ano_res_save_stats` is the "getting bulky" hint, `ano_res_save_delete` is the user-initiated removal. Load: scan newest-seq-first, fresh handle each, first valid wins; orphaned `.tmp` tried last then purged; all fail → sentinel, "start fresh", never garbage. Per-slot commits serialize on an internal save mutex. Migration: in-memory v(n)→v(n+1) chain at load, written back through commit, never in place.

## 11. The public surface

This is V1, the shipped API. Additions only — nothing is ever removed, renamed, or re-signatured. Threading: `ano_res_init` and all `ano_res_mount` calls on the main thread before other threads load (the `ano_log_init` discipline); after the table freezes, reads are stateless and thread-safe, registry mutation is mutex-guarded, same-slot save commits serialize internally.

```c
#include "anoptic_filesystem.h"   // ano_fspath and the OS roots this module composes
#include "anoptic_memory_pools.h" // the allocators the manager owns
#include "anoptic_strings.h"      // anostr_t views, ANOSTR_SID, interning

#define ANO_RES_MAX_MOUNTS 8  // read-only roots beyond the two built-ins
#define ANO_RES_SAVE_KEEP  3  // gamesave generations retained per slot

// A resource handle as a value. rid inline for pointer-free identity checks;
// slot+gen index the registry; {0,0,0} is the failure sentinel. A retired gen
// makes stale copies politely invalid -- lookups yield the sentinel, never UB.
typedef struct { uint64_t rid; uint32_t slot; uint32_t gen; } anores_t;

// -- Lifecycle and mounts -----------------------------------------------------------------

// Pin write root (ano_fs_userpath(), created if absent) and base mount
// (<gamepath>/resources); create the registry and the manager's allocators.
// Main thread, after ano_log_init. Output: 0, or -1 if either root failed.
int  ano_res_init(void);

// Register an additional read-only root, shadowing base and earlier mounts (the
// write root still wins). prefix scopes the mount to a logical subtree ("" = whole
// namespace); interned on registration. One dev-build call site in main().
// Output: 0; -1 on invalid prefix, root.length == 0, or a full table.
int  ano_res_mount(const char *prefix, ano_fspath root);

// -- Identity and handles -----------------------------------------------------------------

// The handle for a logical path, loading and taking ownership on first request
// (single-copy: same path twice = same handle). Category and placement are the
// manager's business. Sentinel handle on refusal or load failure, one log line.
anores_t ano_res_get(const char *logical);

// The whole payload as a byte view -- borrowed, never owned by the caller.
// Valid until the handle's generation retires; empty view on sentinel/stale handles.
anostr_t ano_res_bytes(anores_t h);

// Destructively reclaim the payload: the manager relinquishes the block (the
// Vulkan staging hand-off), the generation retires, outstanding views die.
// Output: the block and its size via out-params; -1 on sentinel/stale handles.
int  ano_res_release(anores_t h, void **data, size_t *size);

// Drop the manager's copy without taking it (level teardown does this in bulk
// via allocator wink-out; this is the single-resource form).
int  ano_res_unload(anores_t h);

// -- Resolution (escape hatch; every call site is migration debt) --------------------------

// Absolute OS path where a logical path's bytes live right now, loose mounts only
// (a packed asset resolves empty). For the transition while parsers still self-open.
ano_fspath ano_res_resolve(const char *logical);

// Where a write to this logical path would land under the write root; parents
// created, so a non-empty result is ready to open.
ano_fspath ano_res_resolve_write(const char *logical);

// Validated join of a relative fragment onto a base directory (the glTF image-URI
// case); kills ad-hoc snprintf joins. length == 0 on invalid input or overflow.
ano_fspath ano_res_subpath(ano_fspath base, const char *relative);

// Whether any mount currently contains the path. ADVISORY ONLY -- metadata caches
// lie (9P/SMB); gate on ano_res_get and handle the sentinel instead.
bool ano_res_exists(const char *logical);

// -- Unowned reads ---------------------------------------------------------------------------

// One-shot gulp into a caller-supplied heap, bypassing the registry: fresh handle,
// fstat as hint only, read loop to EOF, ANO_CACHE_LINE-aligned, one guard NUL, size
// = bytes read. For config bootstrap and genuinely unowned reads; also the internal
// primitive every owned load is built on. data == NULL means failure, never UB.
anostr_t ano_res_slurp(mi_heap_t *heap, const char *logical);

// -- The write path: durable and atomic, always under the write root -----------------------

// Durably replace a fixed-name file (config, keybindings). Full protocol (§10);
// on any error the temp is unlinked, fsync is never retried on the same handle,
// the caller's buffer is the source of truth -- call again to retry.
// Output: 0 only when the replacement is durable on disk; -1 otherwise (previous
// file intact).
int  ano_res_write(const char *logical, const void *data, size_t size);

// Rename a damaged file under the write root to "<name>.broken": regenerate
// defaults without destroying evidence. Output: 0; -1 if absent or rename failed.
int  ano_res_quarantine(const char *logical);

// Commit a gamesave generation: framed payload (§9) via the full protocol to a
// BRAND-NEW filename "saves/<slot>.<seq>.anosave", verified through a fresh read
// handle before older generations are pruned (keep ANO_RES_SAVE_KEEP). Same-slot
// commits serialize internally. Output: 0 when durable AND verified; -1 otherwise
// (every prior generation intact).
int  ano_res_save_commit(const char *slot, uint32_t format_version,
                         const void *payload, size_t size);

// Load the newest VALID gamesave: newest-seq-first, fresh handle each, framing +
// hashes validated (never stat metadata), first pass wins; a torn newest degrades
// one generation. Payload as an owned resource under the manager; format_version
// and seq via out-params. Sentinel handle when no valid generation exists.
anores_t ano_res_save_load(const char *slot, uint32_t *format_version, uint64_t *seq);
```

`anoptic_res_graphics.h` (step 3) adds the domain tier on the same grammar — sketch, frozen in shape if not yet in every signature: `ano_resgfx_model(anores_t)` ingests a glTF handle into conditioned mesh/material/skeleton views; `ano_resgfx_image(anores_t)` decodes into a staging-shaped block; both serve typed views into manager memory and `ano_res_release`-style hand-offs to the render bridge; cgltf and stb_image appear nowhere outside `src/resources/graphics/`. Step 5 adds the async four: `ano_res_get_async`, `ano_res_poll`, `ano_res_pump`, the ticket.

## 12. The sequence

Each step independently mergeable. **Lands** = new capability. **Deletes** = the hardcoded thing it kills. **Bar** = merge condition.

### Step 0 — the memory tier
- **Lands:** `anoptic_memory_pools.h`: multipool, monotonic, pool (stripe waits for its consumer); parent-composition; per-allocator stats; wink-out via backing-heap destroy.
- **Deletes:** nothing.
- **Tests:** new `anotest_mempools` (`unit;mem`): size-class correctness fuzz, exhaustion behavior (false-on-empty, never UB), wink-out leak oracle under ASan/mi-debug, stats accuracy; `anotest_mempoolbench` (bench, DISABLED in ctest): the Lakos grid — alloc/free churn, batch-and-wink, composition — vs raw mi_heap baseline.
- **Bar:** multipool ≥ mi_heap on churn shapes, monotonic+wink ≥ both on batch shapes, zero leaks under the oracle.

### Step 1 — namespace, registry, handles, read path
- **Lands:** `anoptic_resources.h` core + platform TUs: init/mount/get/bytes/release/unload/slurp/resolve/resolve_write/subpath/exists; the registry (intern-table shape, generations); CMake `ANO_DEV_RESOURCES` and the install rule (exe + `resources/`). Interim placement: model A (one multipool) as the null hypothesis the bake-off must beat.
- **Deletes:** nothing.
- **Tests:** new `anotest_resources` (`unit;mem`): hostile-path fuzz (all refuse, none UB); shadow order; read-contract oracles (byte-identical, size == bytes written, guard NUL, alignment, absent file → sentinel + one log line); handle oracles (double-get = same handle, stale-gen access = sentinel view, release retires views). Scratch dirs per `templates/scratch.h`.
- **Bar:** any code loads any staged file by logical name from any CWD, Debug and Release, both toolchains; no way to reach UB through the public surface.

### Step 2 — shaders ride it
- **Lands:** all ~20 `loadFile` sites become `ano_res_get`/`ano_res_bytes`; pipeline build consumes views, no copies.
- **Deletes:** `loadFile`, `openEngineFile`, `struct Buffer`.
- **Tests:** existing vk suite green; one manual run from a foreign CWD.
- **Bar:** zero shader-path strings outside `ano_res_get` calls.

### Step 3 — `anoptic_res_graphics.h`; the shim dies; the bake-off runs
- **Lands:** glTF/image/font ingest inside `src/resources/graphics/` (cgltf, stb_image internal; fonts as blobs + `FT_New_Memory_Face`); parse staging in a monotonic arena, winked out per ingest; models to `resources/models/` or the `"models/"` prefix graft. With real assets flowing, the §5 bake-off runs: models A–E implemented behind the frozen surface, benched, winner recorded here with numbers.
- **Deletes:** the chdir shim (`main.c:588`), then `ano_fs_chdir_gamepath`; the hand-rolled join in `text_raster.c`; all renderer-side file opens and glTF parsing; loose `malloc`/`free` in the ingest path.
- **Tests:** engine smoke from a foreign CWD on both OSes; installed-tree run; `anotest_text` green; ingest equivalence (res_graphics scene renders the old path's frame, render suite as oracle); `anotest_resbench` first series = the bake-off grid.
- **Bar:** CWD is irrelevant; nothing outside `src/resources/` and the logger opens a file by path (grep-enforceable); the hierarchy decision is recorded with numbers.

### Step 4 — durable writes, saves, first write clients
- **Lands:** `ano_res_write`, `ano_res_quarantine`, `ano_res_save_commit`/`_load` per §10. Immediate clients: `anoptic_config.h` (jsmn over `ano_res_slurp`, `ANOSTR_SID`-keyed typed store, quarantine-and-regenerate, one real settings file) and keybindings as a config domain (scancode → SID action-id table; hardcoded GLFW switches in `main.c`/`instanceInit.c` become action dispatches).
- **Deletes:** hardcoded key handling; the no-settings-persist state.
- **Tests:** frame round-trip + corruption battery (truncate, bit-flip header vs body, rename-masquerade — all detected, degrade one generation); fault-injection harness (`#ifdef`-gated child killed at every protocol step, parent asserts old-or-new-complete); config round-trip + quarantine.
- **Bar:** `kill -9` at any instant leaves every user file readable; corrupt config cannot block boot; rebinds survive relaunch.

### Step 5 — the transport (port, not design)
- **Lands:** the logger's variable-length MPSC ring moves to `anoptic_collections.h` as the generic ring (logger re-consumes it). On it: request ring, one IO thread (drainer's park/wake, lap-counter reclaim, shutdown discipline verbatim), blocking `pread` + `posix_fadvise`, per-consumer SPSC completion rings drained by a per-frame pump. Public: `ano_res_get_async`, `ano_res_poll`, `ano_res_pump`, the 8-byte ticket. Completions polled, never callbacks. Two bands, NOW and LATER; a blocking wait on a LATER ticket boosts it. Missing file completes FAILED through the ticket, identical to sync. Workers call sync loads into worker-owned allocators; ownership transfers through the completion message. Async writes are copy-at-submit.
- **Deletes:** nothing — sync load remains the primitive.
- **Tests:** TSan mandatory (`build.sh 7` under WSL); `anotest_res_async` fuzz, logfuzz-oracle style: N producers under full-ring pressure, every ticket completes exactly once, async bytes == sync bytes, FAILED count == missing-file count; `anotest_resbench` p50/p99/p99.9 per band under a background stream, bulk-load wall time vs step-1 baseline.
- **Bar:** TSan-clean; oracle holds over a soak; a level streams in the background with zero frame hitches.

### Step 6 — the streaming economy
- **Lands:** the chunk pool as `ano_mem_pool` (512 KiB blocks, false-on-empty); real ranged reads (audio/mip shape) as views per §4; LZ4 for latency, plain zstd for bulk, decoded on a worker pipelined so chunk N decodes while N+1 reads; store-raw for already-compressed payloads.
- **Deletes:** nothing; capacity only.
- **Tests:** range correctness vs whole-file oracle (random offset/length fuzz); pool exhaustion returns false-on-empty, never blocks the IO thread; `anotest_resbench` compressed corpus.
- **Bar:** effective bandwidth on compressed assets exceeds raw drive bandwidth, or the codec work reverts.

### Step 7 — the pack and the bake
- **Lands:** `anopak` mount per §9; ~200-line offline builder wired into install. Load-in-place bake for models end-to-end: PODS image, pointers as offsets, one fix-up loop at load, `memcpy`/union punning only, zero runtime parsing — the res_graphics ingest degenerates to gulp + fix-up into the same allocator, same handle out. Loose files keep shadowing packs. Dev hot reload rides the completion ring: 500 ms mtime+size poll, confirmed by content hash before a frame-boundary swap.
- **Deletes:** runtime JSON parsing for baked models; cgltf leaves the shipped path (dev-import tool only), retiring the last `ano_res_resolve` debt.
- **Tests:** bake determinism (byte-identical pack); TOC bit-flip refuses at mount; shadow test; load-equivalence (baked model renders identically to the cgltf path); `anotest_resbench` TOC-lookup series.
- **Bar:** the demo scene loads with zero parse work and zero path strings at runtime.

### Step 8 — parallel pread, on demand only
- **Lands (maybe never):** 2–4 IO threads on the same request ring.
- **Bar to start:** `anotest_resbench` shows rung 0 leaving the drive idle while requests queue. **Bar to merge:** beats single-thread p99 on the streaming series. io_uring/IOCP remain a recorded rung below this one.

## 13. Standing rejections

| Rejected | The flat alternative that won |
|---|---|
| the librarian model (caller-owned bytes, manager owns nothing, parses nothing) | owner model: manager-owned allocators, handles, views, explicit release |
| reference-counted lifetimes | wink-out by lifetime arena; model C must answer cross-level sharing before refcounts get a rehearing |
| four priority bands + byte budgets | two bands + boost-on-wait, until a starvation bench |
| io_uring / IOCP / SQPOLL / O_DIRECT default | parallel `pread`; keep the warm dev loop |
| mmap as a load path | fault stalls, SIGBUS over 9P/SMB (CIDR'22) |
| zstd dictionaries | plain zstd until a pack bench shows the gap |
| sectioned files (4-way) | two destinations: CPU blob, GPU staging (model D formalizes this) |
| composite-resource integrity graphs | one baked contiguous image + integer ids |
| GPU decompression (GDeflate) dependency | reserved codec byte only |
| SQLite savefiles | WAL/fcntl broken over NFS/SMB/9P |
| 128-bit GUIDs + import databases | hashed logical paths; solo scale renames are greps |
| callbacks from the IO thread | polled completions at frame boundaries (house pattern) |
| lock-free registry/resolve | rings for queues, mutex for maps |
| pointer files / mtime save selection | generation scan; mtime untrustworthy on 9P/SMB |
| migrating logger/crash onto the resource manager | logging is monolithic by design; filesystem keeps its clients |

## 14. Done means

- Installed tree (exe + `resources/`) runs from any CWD on Windows and Linux; nothing outside `src/resources/` and the logger opens a file.
- Every loadable lives in a manager-owned allocator chosen by the recorded bake-off winner; teardown of any lifetime group is one wink-out; zero loose `malloc`/`free` in any ingest path.
- Every consumer holds `anores_t` handles or views; stale access is a sentinel, never UB; GPU hand-off is a destructive release, never a copy the bench didn't approve.
- `kill -9` at every protocol step: every user file old-complete or new-complete; a torn newest save degrades one generation, silently to the user, loudly in the log.
- Every resource-naming site is an `ANOSTR_SID` literal or an interned runtime string; no raw filename keys anywhere.
- TSan-clean transport whose fuzz oracle holds over a soak; streaming a level produces zero frame hitches.
- Compressed assets read faster than raw drive bandwidth; the baked demo scene loads with zero runtime parsing.

## 15. Sources

Gregory *GEA* 3rd ed. ch. 6 §6.2 (allocators, handles), ch. 7 §7.2 (the resource manager proper); Lakos, *Local ('Arena') Memory Allocators*, CppCon 2017 (`docs/references/John_Lakos_Slides.pdf` — DVLUC, the 14 strategies, wink-out); Bloomberg BDE `bdlma` (multipool/sequential/concurrent shapes); Haas & Leis VLDB'23; Patterson et al. SOSP'95 (TIP); Crotty et al. CIDR'22; Pillai et al. OSDI'14 + PostgreSQL fsyncgate; Didona et al. SYSTOR'22; Costa *Modern Storage is Plenty Fast*; DirectStorage 1.4 + GACL (GDC 2026); TigerBeetle; sokol_fetch; Unreal IoStore/Zen; Godot `res://`/`user://`; SQLite how-to-corrupt docs. In-tree: `anoptic_strings.h`/`anoptic_strings_utf.h` (the value/view/intern/reclaim grammar), `src/log/log_ring.h` + `log_core.c`, `docs/text/logger.md`, render bridge conventions, `tests/templates/`, the WSL-9P incident fix (`b589d43`).
