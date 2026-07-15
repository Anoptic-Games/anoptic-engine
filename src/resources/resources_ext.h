/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Extension registry. MEANING: what a resource IS.
// Kind is a FOURCC tag, interned to a dense id by res_ext_freeze() (sorted by fourcc). Dense id never reaches disk.
// Adding a resource class registers ONE res_ext.

#ifndef ANOPTIC_RESOURCES_EXT_H
#define ANOPTIC_RESOURCES_EXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "resources_internal.h"

#define ANO_FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b)<<8) | \
                             ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))
#define RES_KIND_MAX 32u    // dense-id bound
#define RES_EXT_MAX  16u

// Core builtin kind. Dense id 0. Every unclassified byte string is one.
#define RES_TAG_BYTES  ANO_FOURCC('B','Y','T','S')

// Extension-owned kinds. Listed so routing/block framing can name them.
#define RES_TAG_GFX_SCENE    ANO_FOURCC('R','G','F','X')   // conditioned glTF scene
#define RES_TAG_GFX_BINDING  ANO_FOURCC('G','B','N','D')   // derived GPU binding table
#define RES_TAG_IMAGE_ENC    ANO_FOURCC('I','E','N','C')   // encoded image bytes
#define RES_TAG_IMAGE_DEC    ANO_FOURCC('I','D','E','C')   // decoded RGBA8 pixels
#define RES_TAG_AUDIO_PCM    ANO_FOURCC('A','P','C','M')   // planar f32 PCM
#define RES_TAG_SCRIPT_BC    ANO_FOURCC('S','B','C','D')   // script bytecode
#define RES_TAG_LEVEL        ANO_FOURCC('L','V','L','0')   // conditioned level
#define RES_TAG_FONT         ANO_FOURCC('F','O','N','T')
#define RES_TAG_SHADER       ANO_FOURCC('S','H','D','R')
#define RES_TAG_SAVE         ANO_FOURCC('S','A','V','E')
#define RES_TAG_CONFIG       ANO_FOURCC('C','F','G','0')
#define RES_TAG_WORLD        ANO_FOURCC('W','R','L','D')
#define RES_TAG_TOOL         ANO_FOURCC('T','O','O','L')

typedef struct res_ext_kind {
    uint32_t    tag;        // ANO_FOURCC(...). Stable on disk and in packs.
    const char *name;       // "graphics.scene"
    bool        derived;    // never resolvable from the filesystem
    bool        bakeable;   // validate() is MANDATORY on RES_PROVENANCE_PACK adopt
} res_ext_kind;

typedef struct res_derive_out {
    res_owned_block     block;
    uint32_t            tag;
    char                display[128];        // diagnostic only. NEVER hashed
    res_dependency_meta deps[RES_DEPS_MAX];
    size_t              dep_count;
} res_derive_out;

typedef struct res_ext {
    const char *name;                                    // "graphics"
    const res_ext_kind *kinds; size_t kind_count;

    uint32_t (*classify)(const char *logical, size_t len);          // 0 = not mine
    int      (*derive)  (const ano_res_read *, anores_t src, ano_res_lifetime,
                         uint32_t want_tag, res_derive_out *out);
    int      (*validate)(uint32_t tag, const void *block, size_t size);   // hostile-input gate
    int      (*deps_of) (uint32_t tag, const void *block, size_t size,
                         res_dependency_meta *out, size_t cap, size_t *n);
    res_disposition (*share_policy)(uint32_t tag);
} res_ext;

int      res_ext_register(const res_ext *);   // owner thread, before res_ext_freeze()
void     res_ext_freeze(void);                // SORTS kinds by fourcc -> deterministic dense ids
void     res_ext_reset(void);                 // res_registry_shutdown. roster is rebuildable
uint16_t res_kind_of(uint32_t tag);           // dense id. 0 = unknown
uint32_t res_tag_of(uint16_t kind);
bool     res_kind_derived(uint16_t kind);
bool     res_kind_bakeable(uint16_t kind);

// classify() walk. RES_TAG_BYTES when nobody claims it.
uint32_t res_tag_from_path(const char *logical, size_t len);

// Extension that owns tag, or NULL.
const res_ext *res_ext_of_tag(uint32_t tag);

#endif // ANOPTIC_RESOURCES_EXT_H
