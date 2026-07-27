<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# String Identity: `ANOSTR_SID` and Friends

Usage doc for the identity primitives in `include/anoptic_strings.h`.
Keys things by strings without paying for strings. Value type (`anostr_t`), builder, and byte-level ops live in the header.
This doc: compile-time sid (**s**tring **id**), runtime intern table, and call-site patterns.
Sketches (event bus, resource registry) show intended call-site shape, not a shipped API.

---

## The two primitives, side by side

| | `ANOSTR_SID` / `ANOSTR_SID32` | `anostr_intern_t` + `anostr_sym` |
|---|---|---|
| id assigned | at **build time** (FNV-1a of the literal) | at **runtime** (first-sight insertion) |
| id shape | sparse 64/32-bit hash | dense `u32`, `0..count-1` |
| input | string **literals only** | any `anostr_t`, discovered at runtime |
| string bytes at runtime | **none** (number only) | one canonical copy in the table's heap |
| reverse lookup (id → string) | no (see the X-macro pattern below) | yes, `anostr_sym_str` |
| usable as `case` label / enum / array size | yes (true integer constant expression) | no |
| good for | keys known when you write the code | keys discovered while the game runs |
| indexing arrays | no (sparse) | yes (dense) |

They share one hash function:

```c
ANOSTR_SID(x)   == anostr_hash  (anostr_lit(x))   // always
ANOSTR_SID32(x) == anostr_hash32(anostr_lit(x))   // always
```

Runtime strings (scene file, console, socket) hash into the same key space as baked constants. Every scenario below uses that bridge.

## What a sid *is*: an integer named by a string

A sid is a plain integer constant named by a string. Same relationship as an `enum` value to its identifier:

```c
enum { RED };                  // RED is the integer 0,          spelled "RED"
ANOSTR_SID("player_spawn")     //     is the integer 0x9d2e4c…,  spelled "player_spawn"
```

Enum counts (0, 1, 2…). Sid hashes the name: any string works; TUs agree without a shared header.

The string is **provenance, not content.** Macro folds to the number; literal discarded. No reverse table. `case ANOSTR_SID("spawn"):` is `case 0x…:`. One-way and lossy:

```
"player_spawn"  ──FNV──▶  0x9d2e4c…        compute the id from the name   ✓
0x9d2e4c…       ──✗──▶    "player_spawn"    recover the name from the id   ✗ (it is gone)
```

A sid answers *"same or not?"*: compare, `switch`, map key. No read/slice/concat/sort/print. Identity **token**, not a string. (Not a C "string constant": that means the literal bytes.) Name recovery: intern table or X-macro below.

## Scenario 1: event types (event bus, Step 8) *(sketch)*

The sender names the event; the compiler turns the name into a number; the receiver switches
on numbers. No registration step, no central enum to maintain, no hashing at either end.

```c
// Sender: the key is an immediate in the instruction stream.
bus_publish(ANOSTR_SID("player_spawn"), &(spawn_payload){ .pos = p });

// Receiver: O(log K) dispatch, duplicate names in one switch are a COMPILE ERROR
// (duplicate case values), so intra-switch hash collisions cannot ship silently.
void on_event(anostr_sid type, const void *payload)
{
    switch (type) {
    case ANOSTR_SID("player_spawn"):  spawn_avatar(payload);   break;
    case ANOSTR_SID("player_death"):  drop_inventory(payload); break;
    case ANOSTR_SID("level_loaded"):  bind_navmesh(payload);   break;
    default:                          break;      // unknown events are legal traffic
    }
}
```

`anotest_sidbench` (5950X, -O3, 16 types): strcmp chain 24 ns/event, runtime-hash then switch 16 ns, baked sid switch 9 ns. Chain is O(K); switch stays flat.

## Scenario 2: data-driven content meets compiled handlers

The file says `"point_light"`; the code was compiled knowing `"point_light"`. Hash the
runtime string once and the two meet:

```c
// Scene loader: component names parsed out of JSON/glTF extras.
anostr_t typeName = /* slice of the parsed buffer, zero-copy */;
switch (anostr_hash(typeName)) {
case ANOSTR_SID("point_light"):  add_point_light(entity, props);  break;
case ANOSTR_SID("mesh"):         add_mesh(entity, props);         break;
case ANOSTR_SID("rigid_body"):   add_rigid_body(entity, props);   break;
default:
    ano_log(ANO_WARN, "unknown component '%.*s'", anostr_fmt(typeName));
}
```

Pattern for every "text outside, integers inside" boundary: scene files, prefabs, animation event tracks, particle descriptors.

## Scenario 3: asset keys / resource GUIDs *(sketch, see `docs/resourcemgr/resource-manager.md`)*

Resource registry GUID is the hashed path. Build-time call sites bake the key; mod loaders and hot-reload watchers hash discovered paths. One registry, one key type, both producers:

```c
// Compiled call site: no path string in the binary, no hash at runtime.
model_handle crate = resmg_acquire(ANOSTR_SID("assets/props/crate.gltf"));

// Mod loader: same key space, computed once per discovered file at scan time.
anostr_sid key = anostr_hash(modFilePath);
resmg_register(key, modFilePath /* kept for reload + diagnostics */);
```

Rule of thumb: the *reference* is a sid; the *inventory* (enumerate, display, reload) also keeps the string, interned.

## Scenario 4: material / shader parameters, `SID32` and packed stores

Parameter blocks are small and hot; a 4-byte key halves the header traffic and matches GPU
constant layouts. This is what `ANOSTR_SID32` is for:

```c
typedef struct {
    anostr_sid32 param;     // 4 bytes, baked: ANOSTR_SID32("u_roughness")
    uint32_t     offset;    // byte offset into the UBO
} param_slot;

mat_set_f32(mat, ANOSTR_SID32("u_roughness"), 0.35f);
mat_set_f32(mat, ANOSTR_SID32("u_metallic"),  1.0f);
```

Shader reflection hashes SPIR-V names once at pipeline build; draw-time lookup is int-vs-int. SID32 birthday bound hits around tens of thousands of keys: fine for parameters, use 64-bit for global asset registries.

## Scenario 5: config keys and cvars

```c
// Parsing user config: unknown keys warn instead of failing, forward-compatible.
switch (anostr_hash(key)) {
case ANOSTR_SID("graphics.width"):           cfg->width  = parse_u32(val); break;
case ANOSTR_SID("graphics.height"):          cfg->height = parse_u32(val); break;
case ANOSTR_SID("graphics.fullscreen-mode"): cfg->fs     = parse_fs(val);  break;
default:
    ano_log(ANO_WARN, "config: unknown key '%.*s'", anostr_fmt(key));
}
```

## Scenario 6: editor console and tooling

Console input is the canonical "string at runtime, handler at compile time" case:

```c
void console_exec(anostr_t line)
{
    anostr_t cmd = anostr_slice(line, 0, anostr_find(line, anostr_lit(" "), 0));
    switch (anostr_hash(cmd)) {
    case ANOSTR_SID("spawn"):      cmd_spawn(line);      break;
    case ANOSTR_SID("teleport"):   cmd_teleport(line);   break;
    case ANOSTR_SID("profile"):    cmd_profile(line);    break;
    case ANOSTR_SID("screenshot"): cmd_screenshot(line); break;
    default:                       console_print("unknown command");
    }
}
```

Same shape for editor gizmo modes, undo-op type tags, and panel ids.

## Scenario 7: wire and disk formats: ids stable by construction

Enum ordinals drift on reorder; a sid changes only if the *name* changes. Right tag for:

- **network message types**: both peers hash the same name; no shared header, no ordinal drift;
- **save-file field tags**: stable across refactor/insert/reorder; rename is a *visible* format break.

```c
// Serializer: tagged fields, order-independent, skippable-by-unknown.
write_field(out, ANOSTR_SID32("pos"),    &t->pos,    sizeof t->pos);
write_field(out, ANOSTR_SID32("vel"),    &t->vel,    sizeof t->vel);
write_field(out, ANOSTR_SID32("health"), &t->health, sizeof t->health);
```

## The X-macro pattern: names in dev, numbers in release

The sid ships no string. For a switchable constant *and* a human-readable name in dev builds, define the list once:

```c
#define EVENT_LIST(X)      \
    X(EV_SPAWN, "player_spawn")  \
    X(EV_DEATH, "player_death")  \
    X(EV_LEVEL, "level_loaded")

#define AS_CASE(sym, s) case ANOSTR_SID(s): /* ... */ break;

#ifdef DEBUG_BUILD
// Dev-only reverse map for logs and inspectors; absent from release entirely.
static const char *event_name(anostr_sid id)
{
    switch (id) {
#define AS_NAME(sym, s) case ANOSTR_SID(s): return s;
    EVENT_LIST(AS_NAME)
#undef AS_NAME
    default: return "?";
    }
}
#endif
```

`anotest_sidbench.c` uses exactly this shape to generate its case labels, name table, and
sid table from one list.

## When NOT to use a sid

- **The key is not a literal.** `ANOSTR_SID` is literals only. Runtime strings: `anostr_hash` (same key space), or intern for bytes/dense ids.
- **You need to index an array or iterate the key set.** Sids are sparse. `anostr_sym` is dense `0..count-1` for side arrays, iteration, `anostr_sym_sort`.
- **You need the string back in release.** Intern it, or keep the X-macro name table in all builds.
- **Cross-switch collisions at huge scale.** Compiler catches dupes *within one switch* only. 64-bit FNV-1a is fine at engine scale; `SID32` stays in bounded namespaces (parameters, field tags, message types).

## Limits, for reference

- `ANOSTR_SID_MAX` = 128 bytes; a longer literal is a **compile error** (negative array
  size), never a truncation.
- Bytes hashed = `sizeof(lit) - 1`: embedded `\0` bytes count, exactly like `anostr_lit`.
- The macro folds to an immediate at every `-O` level, `-O0` included (clang and gcc; the
  ICE-context use is a GNU extension, per the engine's gnu23 build policy).
- Measured (5950X, -O3, `anotest_sidbench`): sid switch 9.1 ns/event vs strcmp chain 24.1, runtime-hash-then-switch 16.4, `anostr_intern_find` 16.8; bulk-keying 20k ids: 58 ns/key intern, 0 with sids (ids already in `.rodata`).
- Bulk reads (same rig, 50k records × 4 keying, 2M lookups): sid→index ~6 ns (136 M/s); `anostr_intern_find` ~62 ns; hash-plus-`anostr_eq` ~47 ns; sorted-sid `bsearch` ~96 ns. Baked sid touches no string bytes.
