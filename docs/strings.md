<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# String Identity: `ANOSTR_SID` and Friends

**Status:** how to use the identity primitives in `include/anoptic_strings.h`.
**Scope:** when and how to key things by strings without paying for strings. The value type (`anostr_t`), builder, and byte-level ops are specified in the header itself; this doc covers the two identity primitives — the compile-time sid and the runtime intern table — and the patterns that show up in real engine and editor call sites. Several examples sketch subsystems that are not built yet (event bus, resource registry); those are marked as sketches and show the intended call-site shape, not a shipped API.

---

## The two primitives, side by side

| | `ANOSTR_SID` / `ANOSTR_SID32` | `anostr_intern_t` + `anostr_sym` |
|---|---|---|
| id assigned | at **build time** (FNV-1a of the literal) | at **runtime** (first-sight insertion) |
| id shape | sparse 64/32-bit hash | dense `u32`, `0..count-1` |
| input | string **literals only** | any `anostr_t`, discovered at runtime |
| string bytes at runtime | **none** — only the number ships | one canonical copy in the table's heap |
| reverse lookup (id → string) | no (see the X-macro pattern below) | yes, `anostr_sym_str` |
| usable as `case` label / enum / array size | yes (true integer constant expression) | no |
| good for | keys known when you write the code | keys discovered while the game runs |
| indexing arrays | no — sparse | yes — dense by design |

They share one hash function, and that is what ties them together:

```c
ANOSTR_SID(x)   == anostr_hash  (anostr_lit(x))   // always
ANOSTR_SID32(x) == anostr_hash32(anostr_lit(x))   // always
```

日本語🟩

Any string that *arrives* at runtime — parsed from a scene file, typed into a console, read off a socket — gets `anostr_hash`ed and lands in the same key space as the constants the compiler baked. That bridge is what every scenario below leans on.

## Scenario 1 — event types (event bus, Step 8) *(sketch)*

The sender names the event; the compiler turns the name into a number; the receiver switches on numbers. No registration step, no central enum to maintain, no hashing at either end.

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

Compare the alternatives measured in `anotest_sidbench` (5950X, -O3, 16 types): a strcmp chain costs 24 ns/event, hashing the name at runtime then switching costs 16 ns, the baked sid switch costs 9 ns — and the chain approaches scale O(K) while the switch stays flat.

## Scenario 2 — data-driven content meets compiled handlers

The file says `"point_light"`; the code was compiled knowing `"point_light"`. Hash the runtime string once and the two meet:

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

This is the pattern for every "text outside, integers inside" boundary: scene files, prefab definitions, animation event tracks, particle system descriptors.

## Scenario 3 — asset keys / resource GUIDs *(sketch, see `docs/resource-manager.md`)*

The resource registry's GUID is the hashed path. Call sites that know their asset at build time bake the key; mod loaders and hot-reload watchers hash the paths they discover. One registry, one key type, both producers:

```c
// Compiled call site: no path string in the binary, no hash at runtime.
model_handle crate = resmg_acquire(ANOSTR_SID("assets/props/crate.gltf"));

// Mod loader: same key space, computed once per discovered file at scan time.
anostr_sid key = anostr_hash(modFilePath);
resmg_register(key, modFilePath /* kept for reload + diagnostics */);
```

Rule of thumb: the *reference* is a sid; the *inventory* (which needs to enumerate, display, and reload) also keeps the string — interned, not duplicated.

## Scenario 4 — material / shader parameters, `SID32` and packed stores

Parameter blocks are small and hot; a 4-byte key halves the header traffic and matches GPU constant layouts. This is what `ANOSTR_SID32` is for:

```c
typedef struct {
    anostr_sid32 param;     // 4 bytes, baked: ANOSTR_SID32("u_roughness")
    uint32_t     offset;    // byte offset into the UBO
} param_slot;

mat_set_f32(mat, ANOSTR_SID32("u_roughness"), 0.35f);
mat_set_f32(mat, ANOSTR_SID32("u_metallic"),  1.0f);
```

The shader-side reflection pass hashes the names it finds in the SPIR-V once at pipeline build, so lookup at draw time is integer-vs-integer. 32-bit collisions become a real concern around tens of thousands of distinct keys (birthday bound); parameter namespaces are nowhere near that, but a global asset registry is — default to 64-bit there.

## Scenario 5 — config keys and cvars

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

## Scenario 6 — editor console and tooling

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

Same shape for editor gizmo modes, undo-op type tags, and panel ids — anywhere the editor wires named things to code paths.

## Scenario 7 — wire and disk formats: ids stable by construction

An enum ordinal changes when someone reorders the enum; a sid changes only if the *name* changes. That makes sids the right tag for:

- **network message types** — both peers compute the same id from the same name; no shared
  header to keep in sync, no ordinal drift between client and server builds;
- **save-file field tags** — a field keeps its tag across refactors, insertions, and
  reorderings; renaming a field is *visibly* a format break, which is what you want.

```c
// Serializer: tagged fields, order-independent, skippable-by-unknown.
write_field(out, ANOSTR_SID32("pos"),    &t->pos,    sizeof t->pos);
write_field(out, ANOSTR_SID32("vel"),    &t->vel,    sizeof t->vel);
write_field(out, ANOSTR_SID32("health"), &t->health, sizeof t->health);
```

## The X-macro pattern: names in dev, numbers in release

The sid deliberately ships no string. When a subsystem wants both the switchable constant *and* a human-readable name in dev builds, define the list once:

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

`anotest_sidbench.c` uses exactly this shape to generate its case labels, name table, and sid table from one list.

## When NOT to use a sid

- **The key is not a literal.** `ANOSTR_SID` takes string literals only, by design. Strings
  discovered at runtime get `anostr_hash` (same key space) or, when you also need the bytes
  back or dense ids, the intern table.
- **You need to index an array or iterate the key set.** Sids are sparse hashes. The intern
  table's `anostr_sym` is dense `0..count-1` for exactly this — per-symbol side arrays,
  iteration, `anostr_sym_sort`.
- **You need the string back in release.** It does not exist. Intern it, or keep the
  X-macro name table in all builds for that subsystem.
- **Cross-switch collisions matter at huge scale.** The compiler catches duplicate case
  labels *within one switch*; it cannot see a collision between two unrelated tables. With
  64-bit FNV-1a over engine-scale key counts (thousands to millions) the odds are
  negligible; with `SID32`, stay in bounded namespaces (parameters, field tags, message
  types), not global registries.

## Limits, for reference

- `ANOSTR_SID_MAX` = 128 bytes; a longer literal is a **compile error** (negative array
  size), never a truncation.
- Bytes hashed = `sizeof(lit) - 1`: embedded `\0` bytes count, exactly like `anostr_lit`.
- The macro folds to an immediate at every `-O` level, `-O0` included (clang and gcc; the
  ICE-context use is a GNU extension, per the engine's gnu23 build policy).
- Measured (5950X, -O3, `anotest_sidbench`): sid switch 9.1 ns/event vs strcmp chain 24.1,
  runtime-hash-then-switch 16.4, `anostr_intern_find` 16.8; bulk-keying 20k identifiers
  costs 58 ns/key through the intern table and 0 with sids — the ids are in `.rodata`
  before the process starts.
