# Text Demo: from string literal to rendered glyph

Type Ancient Greek or Elder Futhark into a C string literal and it renders as anti-aliased vector glyphs, swept on the GPU. This is the map between those two points, written around the demo text (Homer and the Gallehus horn). It also records the byte-count refactor that made the styled-run tables safe to edit.

`FONT_RENDER.md` is the rasterizer design doc. This covers the layer above: fonts, bake ranges, shaping, the two display lanes, and how demo text is authored.

---

## The pipeline at a glance

```
 UTF-8 string literal (main.c / text_raster.c — Greek and runes typed verbatim)
        │
        ▼
 ano_text_shape / ano_text_shape_runs          src/text/text_shape.c   (any thread)
   codepoint → bake slot → kerned pen walk
        │  emits AnoGlyphInstance[] (48 B GPU ABI: inverse 2×2, color, pen origin, slot)
        ▼
 per-frame instance buffer                     src/vulkan_backend/text_raster.c
   [0, 8192)   screen overlay region (OSD + logic HUD blocks)
   [8192, …)   world panel region (shaped once at init)
        ▼
 textraster.comp — Scanline Sweeper            GPU, async compute lane
   analytic coverage from monotone quadratic Béziers; no atlas, no SDF
        ▼
 composite                                     GPU, graphics queue
   overlay: premultiplied blend onto the swapchain
   world:   textworld.vert/.frag draw the panel quad in the 3D scene
```

Everything upstream of the GPU is plain data over an immutable `AnoFontBake`. Shaping runs on whatever thread holds the text: the render thread for the OSD, the logic thread for HUD blocks (shipped over the render bridge as named blocks, `RCMD_TEXT_SET`).

## Fonts and bake ranges

One bake serves every lane, assembled at init in `ano_vk_text_init` (`src/vulkan_backend/text_raster.c`) from three faces:

| Face | Ranges | Why |
|---|---|---|
| Geist Regular | ASCII `0020–007E`, Latin-1 `00A0–00FF`, Cyrillic `0400–045F` | the engine's UI face |
| Noto Sans Regular | Greek & Coptic `0370–03FF`, Greek Extended `1F00–1FFF` | Geist has ~no Greek. Extended carries Homer's polytonic accents |
| Noto Sans Runic | Runic `16A0–16F8` | Elder Futhark and friends |

The contract (`ano_text_font_bake_ranges`, `include/anoptic_text.h`): ranges are codepoint-sorted and disjoint, slots assigned range by range in input order. So the range list interleaves faces: Greek sits between Latin-1 and Cyrillic because that is where its codepoints fall. A missing auxiliary font fails soft: its ranges are skipped, never a dead overlay.

Two flavors of "not there", both graceful. A codepoint outside every baked range advances the pen a fixed half-em gap. A codepoint inside a range the face doesn't cover bakes as a blank `ANO_GLYPH_MISSING` entry and shapes to nothing. Neither draws tofu.

Kerning comes from GPOS pair tables and never bridges faces. The shaper drops the kern chain across size changes, newlines, and gaps.

## UTF-8 in string literals — yes, really

The source files are UTF-8 and C23 clang consumes UTF-8 source natively, so demo text is typed as itself:

```c
n = ano_text_shape_lit(bake,
                       "Ἄνδρα μοι ἔννεπε, Μοῦσα, πολύτροπον",
                       22.0f, homerOrg, aegean, hud, HUD_TEXT_CAP, NULL);
```

A plain `""` literal carries the UTF-8 bytes verbatim. No `u8""`/`char8_t` friction, no `\u` escape soup (the runes used to read `"\u16D6\u16B2 ..."`, unreadable and unreviewable). The `_lit` macros wrap `anostr_lit`, which folds the byte length at compile time. No strlen, no allocation.

The shaper decodes the bytes as UTF-8 (`anostr_rune_next`, malformed bytes become U+FFFD) and looks each codepoint up in the bake's range map. If the glyph is baked, it renders. That is the whole story: put Ancient Greek in a string literal and it displays.

## Styled runs and the byte-count refactor

`ano_text_shape_runs` styles one text with consecutive `AnoTextRun` spans:

```c
typedef struct AnoTextRun {
    uint32_t byteCount;  // this run styles the next byteCount bytes
    float    sizePx;
    float    color[4];
} AnoTextRun;
```

Byte counts are fine for dynamic text, where the caller computed them. The static demo tables hand-rolled them: literal counts like `{ 17, ... } { 101, ... }` beside a comment, with a `static_assert` checking only the total. Hand-counting multi-byte UTF-8 (101 bytes of runes, 69 of Greek) was a trap. A wrong split between runs compiled clean and silently bled one run's color into the next.

The refactor makes boundaries correct by construction. Each run's text is its own macro, the full string is their concatenation, each `byteCount` is `sizeof` its own segment:

```c
#define W_TITLE "Scanline Sweeper\n"
/* ... */
#define W_RUNES "\nᛖᚲ ᚺᛚᛖᚹᚨᚷᚨᛊᛏᛁᛉ ᚺᛟᛚᛏᛁᛃᚨᛉ ᚺᛟᚱᚾᚨ ᛏᚨᚹᛁᛞᛟ"
#define W_GREEK "\nΜῆνιν ἄειδε θεὰ Πηληϊάδεω Ἀχιλῆος"

static const char g_worldText[] =
    W_TITLE W_LANE W_KERN1 W_KERN2 W_KERN3 W_KERN4 W_KERN5 W_RUNES W_GREEK;

static const AnoTextRun worldRuns[] = {
    { sizeof W_TITLE - 1, 72.0f, { 1.00f, 0.78f, 0.32f, 1.0f } },
    /* ... */
    { sizeof W_RUNES - 1, 36.0f, { 0.55f, 0.85f, 1.00f, 1.0f } },
    { sizeof W_GREEK - 1, 34.0f, { 0.75f, 0.95f, 0.80f, 1.0f } },
};
```

Edit a line and its run resizes with it. The checksum `static_assert` became redundant and was removed. The same pattern replaced the HUD title runs in `src/engine/main.c` (`TITLE_HEAD` / `TITLE_TAIL`). `runs_valid` (`src/text/text_shape.c`) still rejects any run list whose byte sum disagrees with the text length.

Two conventions the world table demonstrates, keep them when editing:

- A run starting a new line owns its leading `'\n'`, because `'\n'` steps the pen by its own run's lineHeight: the new line's size, not the old one's.
- Line 3's color splits land inside the kern pairs (`A|V`, `L|T`, `T|o`, `W|a`) on purpose. Same-size runs must not move a glyph, so the line shows that color boundaries preserve kerning.

## The two lanes and what the demo shows

**Screen overlay (HUD).** Instance region `[0, 8192)`. The renderer's OSD occupies the front, logic-thread blocks (`ano_render_text_set` / `_clear` over the bridge) append after it in registry order. The demo blocks in `src/engine/main.c`:

| Block | Content |
|---|---|
| `HUD_TEXT_TITLE` | two-run styled title (`sizeof`-counted runs) |
| `HUD_TEXT_NOTICE` | transient line, clears itself after 15 s (`RCMD_TEXT_CLEAR` proof) |
| `HUD_TEXT_CAM` | camera readout, replaced once per second (REPLACE semantics proof) |
| `HUD_TEXT_UNICODE` | Gallehus horn inscription + Cyrillic/Latin-1 sampler |
| `HUD_TEXT_HOMER` | Odyssey 1.1 — Ἄνδρα μοι ἔννεπε, Μοῦσα, πολύτροπον |

**World panel.** Instance region `[8192, …)`, shaped once at init into every frame slot, drawn by `textworld.vert/.frag` as a quad in the scene (768×352 virtual pixels onto 6.0×2.75 world units). The bake's curve data is resolution-independent, so the same instances survive any camera distance and angle. No re-shape, no mips, no atlas. Its text is the styled-runs table above: title, kerning proof line, the Gallehus inscription, and Iliad 1.1 — Μῆνιν ἄειδε θεὰ Πηληϊάδεω Ἀχιλῆος.

On the quotes: the Gallehus horn inscription (ᛖᚲ ᚺᛚᛖᚹᚨᚷᚨᛊᛏᛁᛉ ᚺᛟᛚᛏᛁᛃᚨᛉ ᚺᛟᚱᚾᚨ ᛏᚨᚹᛁᛞᛟ, "I, Hlewagastiz of Holt, made the horn", ~400 AD) is among the most famous genuine Elder Futhark texts. It beats an Old Norse quote because Viking-age Norse used Younger Futhark, a different Unicode block from the Elder Futhark glyphs we bake.

## File map

| File | Role |
|---|---|
| `include/anoptic_text.h` | public contract: fonts, bake, shaper, `_lit` macros |
| `src/text/text.c` | FreeType lifecycle on a module-owned mimalloc heap |
| `src/text/text_bake.c` | outlines → monotone quads → packed GPU blobs + kern table |
| `src/text/text_shape.c` | the pen walk: UTF-8 → `AnoGlyphInstance[]`, runs, measure |
| `src/vulkan_backend/text_raster.c` | bake assembly, both lanes' pipelines, demo panel text |
| `src/engine/main.c` | logic-thread HUD blocks through the render bridge |
| `resources/fonts/` | Geist, Noto Sans (Greek), Noto Sans Runic, staged exe-relative |
| `FONT_RENDER.md` | design doc for the GPU rasterizer |
