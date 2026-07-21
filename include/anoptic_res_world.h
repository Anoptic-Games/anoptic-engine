/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

<<<<<<< HEAD
// World/game-save schema and migration over framed resource generations.
// Level open/close/view are STUB. Schema types below are the declared surface.
=======
// Production world/game-save schema and migration over framed resource generations.
>>>>>>> block-b1-base

#ifndef ANOPTICENGINE_ANOPTIC_RES_WORLD_H
#define ANOPTICENGINE_ANOPTIC_RES_WORLD_H

#include <stdint.h>

#include "anoptic_filesystem.h"   // MAXPATH
#include "anoptic_math.h"         // mat4
#include "anoptic_resources.h"

#define ANO_RESWORLD_SAVE_VERSION 2u

typedef struct anoresworld_state {
    uint64_t simulation_tick;
    uint64_t world_seed;
    float camera_position[3];
    float camera_yaw;
    float camera_pitch;
    uint32_t flags;
} anoresworld_state;

typedef enum anoresworld_save_status {
    ANO_RESWORLD_OK = 0,
    ANO_RESWORLD_NOT_FOUND,
    ANO_RESWORLD_MIGRATED,
    ANO_RESWORLD_CORRUPT,
    ANO_RESWORLD_READER_TOO_OLD,
    ANO_RESWORLD_UNSUPPORTED_VERSION,
    ANO_RESWORLD_MIGRATION_FAILED,
    ANO_RESWORLD_MIGRATION_WRITE_FAILED,
    ANO_RESWORLD_IO_ERROR,
    ANO_RESWORLD_RESOURCE_ERROR,
    ANO_RESWORLD_INVALID_ARGUMENT,
} anoresworld_save_status;

typedef struct anoresworld_save_info {
    uint32_t source_version;
    uint32_t source_min_reader_version;
    uint64_t source_seq;
} anoresworld_save_info;

int ano_resworld_save_commit(const char *slot, const anoresworld_state *state);
anoresworld_save_status ano_resworld_save_load(ano_res_lifetime lifetime, const char *slot,
                                               anoresworld_state *state,
                                               anoresworld_save_info *info);

<<<<<<< HEAD
/* Level schema */
=======
// ---------------------------------------------------------------------------------------------
// The level schema. FROZEN (freeze item 12).
//
// An `anoptic.level` file (jsmn; no new dependency) conditions into a plane-set block whose
// `assets` array IS the disclosed dependency set -- the level does not merely mention its
// assets, it declares them, and the manager prefetches exactly that set.
//
// A level owns a WORLD_LEVEL domain. Its on_load script parameterizes what actually renders
// and its ambient audio actually feeds the sink; a level whose scripts and audio do nothing
// is scaffolding wearing a domain's name.
>>>>>>> block-b1-base

#define ANO_RESLEVEL_TAG      0x304C564Cu   // 'LVL0'
#define ANO_RESLEVEL_VERSION  1u

typedef enum anoreslevel_asset_kind {
    ANO_RESLEVEL_ASSET_MODEL = 0,
    ANO_RESLEVEL_ASSET_TEXTURE,
    ANO_RESLEVEL_ASSET_AUDIO,
    ANO_RESLEVEL_ASSET_SCRIPT,
} anoreslevel_asset_kind;

<<<<<<< HEAD
// Declared asset: logical path plus expected kind.
typedef struct anoreslevel_asset {
    char     logical[MAXPATH];
    uint32_t kind;                      // anoreslevel_asset_kind
    uint32_t tag;                       // FOURCC it must condition to (0 = raw bytes)
} anoreslevel_asset;

// Placed entity. `asset` indexes assets[]. -1 is a pure light or script host.
=======
// One declared asset: a logical path plus the kind the level expects it to be. A mismatch
// at load is a refusal, not a cast.
typedef struct anoreslevel_asset {
    char     logical[MAXPATH];
    uint32_t kind;                      // anoreslevel_asset_kind
    uint32_t tag;                       // the FOURCC it must condition to (0 = raw bytes)
} anoreslevel_asset;

// One placed entity. `asset` indexes assets[]; -1 is a pure light or a pure script host.
>>>>>>> block-b1-base
typedef struct anoreslevel_entity {
    char     name[64];
    mat4     transform;
    int32_t  asset;                     // into assets[], -1 = none
    int32_t  light;                     // into lights[], -1 = none
<<<<<<< HEAD
    int32_t  script;                    // into assets[] (ANO_RESLEVEL_ASSET_SCRIPT), -1 = none
=======
    int32_t  script;                    // into assets[] (an ANO_RESLEVEL_ASSET_SCRIPT), -1 = none
>>>>>>> block-b1-base
    uint32_t flags;
} anoreslevel_entity;

typedef struct anoreslevel_light {
    float    color[3];
    float    intensity;
    float    range;
    float    inner_cone, outer_cone;
    uint32_t type;                      // anoresgfx_light_type
    uint32_t casts_shadow;
    uint32_t _pad[3];
} anoreslevel_light;

<<<<<<< HEAD
// Level view schema. open/view STUB today: always refuse / zeroed.
typedef struct anoreslevel {
    char name[64];

    const anoreslevel_asset  *assets;   uint32_t asset_count;
=======
// The level view: counted arrays borrowing manager memory, valid until the handle's
// generation retires. A zeroed struct means stale, sentinel, or failed validation.
typedef struct anoreslevel {
    char name[64];

    const anoreslevel_asset  *assets;   uint32_t asset_count;   // == the DISCLOSED dep set
>>>>>>> block-b1-base
    const anoreslevel_entity *entities; uint32_t entity_count;
    const anoreslevel_light  *lights;   uint32_t light_count;

    int32_t  on_load;                   // into assets[] (a script), -1 = none
    int32_t  ambient_audio;             // into assets[] (a clip),   -1 = none
    float    camera_position[3];
    float    camera_yaw, camera_pitch;
    uint64_t world_seed;
} anoreslevel;

<<<<<<< HEAD
// STUB. Always -1.
int ano_reslevel_open (const char *logical, ano_res_lifetime *out_domain, anores_t *out_level);
int ano_reslevel_close(ano_res_lifetime domain);   // STUB -1

// STUB. Always zeroed.
=======
// Open a level into its OWN WORLD_LEVEL domain: condition the block, prefetch every declared
// asset into that domain, compile the on_load script, ingest the ambient clip. Output: 0 and
// a live domain + handle, or -1 (the domain is retired on any failure -- a half-open level
// is not a level). close() retires the domain, which is where its residual footprint is
// measured.
int ano_reslevel_open (const char *logical, ano_res_lifetime *out_domain, anores_t *out_level);
int ano_reslevel_close(ano_res_lifetime domain);

// The level view for an open handle. Its pointers die with read; zeroed on refusal.
>>>>>>> block-b1-base
anoreslevel ano_reslevel_view(const ano_res_read *read, anores_t level);

#endif // ANOPTICENGINE_ANOPTIC_RES_WORLD_H
