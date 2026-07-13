/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Portable world-save payload, v1-to-v2 migration, and source-preserving writeback.

#include <anoptic_res_world.h>

#include <math.h>
#include <string.h>

#define WORLD_V1_BYTES 36u
#define WORLD_V2_BYTES 48u

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

static void putf(uint8_t *p, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    put32(p, bits);
}

static float getf(const uint8_t *p)
{
    uint32_t bits = get32(p);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

static bool state_valid(const anoresworld_state *state)
{
    if (state == NULL || !isfinite(state->camera_yaw) || !isfinite(state->camera_pitch)
        || state->camera_pitch < -1.5708f || state->camera_pitch > 1.5708f)
        return false;
    for (int i = 0; i < 3; i++)
        if (!isfinite(state->camera_position[i])
            || state->camera_position[i] < -10000000.0f
            || state->camera_position[i] > 10000000.0f)
            return false;
    return true;
}

static void encode_v2(const anoresworld_state *state, uint8_t out[WORLD_V2_BYTES])
{
    memcpy(out, "ANOW", 4);
    put32(out + 4, 2);
    put64(out + 8, state->simulation_tick);
    put64(out + 16, state->world_seed);
    putf(out + 24, state->camera_position[0]);
    putf(out + 28, state->camera_position[1]);
    putf(out + 32, state->camera_position[2]);
    putf(out + 36, state->camera_yaw);
    putf(out + 40, state->camera_pitch);
    put32(out + 44, state->flags);
}

static bool decode_v1(const uint8_t *bytes, size_t len, anoresworld_state *state)
{
    if (len != WORLD_V1_BYTES || memcmp(bytes, "ANOW", 4) != 0 || get32(bytes + 4) != 1)
        return false;
    *state = (anoresworld_state){
        .simulation_tick = get64(bytes + 8),
        .world_seed = get64(bytes + 16),
        .camera_position = { getf(bytes + 24), getf(bytes + 28), getf(bytes + 32) },
        .camera_yaw = 0.0f,
        .camera_pitch = -0.211f,
        .flags = 0,
    };
    return state_valid(state);
}

static bool decode_v2(const uint8_t *bytes, size_t len, anoresworld_state *state)
{
    if (len != WORLD_V2_BYTES || memcmp(bytes, "ANOW", 4) != 0 || get32(bytes + 4) != 2)
        return false;
    *state = (anoresworld_state){
        .simulation_tick = get64(bytes + 8),
        .world_seed = get64(bytes + 16),
        .camera_position = { getf(bytes + 24), getf(bytes + 28), getf(bytes + 32) },
        .camera_yaw = getf(bytes + 36),
        .camera_pitch = getf(bytes + 40),
        .flags = get32(bytes + 44),
    };
    return state_valid(state);
}

int ano_resworld_save_commit(const char *slot, const anoresworld_state *state)
{
    if (!state_valid(state))
        return -1;
    uint8_t payload[WORLD_V2_BYTES];
    encode_v2(state, payload);
    return ano_res_save_commit_ex(slot, ANO_RESWORLD_SAVE_VERSION,
                                  ANO_RESWORLD_SAVE_VERSION, payload, sizeof payload);
}

static anoresworld_save_status map_raw_status(ano_res_save_status status)
{
    switch (status) {
    case ANO_RES_SAVE_NOT_FOUND:       return ANO_RESWORLD_NOT_FOUND;
    case ANO_RES_SAVE_CORRUPT:         return ANO_RESWORLD_CORRUPT;
    case ANO_RES_SAVE_READER_TOO_OLD:  return ANO_RESWORLD_READER_TOO_OLD;
    case ANO_RES_SAVE_IO_ERROR:        return ANO_RESWORLD_IO_ERROR;
    case ANO_RES_SAVE_RESOURCE_ERROR:  return ANO_RESWORLD_RESOURCE_ERROR;
    case ANO_RES_SAVE_INVALID_ARGUMENT:return ANO_RESWORLD_INVALID_ARGUMENT;
    case ANO_RES_SAVE_OK:              return ANO_RESWORLD_OK;
    }
    return ANO_RESWORLD_RESOURCE_ERROR;
}

anoresworld_save_status ano_resworld_save_load(ano_res_lifetime lifetime, const char *slot,
                                               anoresworld_state *state,
                                               anoresworld_save_info *info)
{
    if (state == NULL || slot == NULL)
        return ANO_RESWORLD_INVALID_ARGUMENT;
    ano_res_save_result raw = {0};
    ano_res_save_status raw_status = ano_res_save_load_ex(lifetime, slot,
                                                          ANO_RESWORLD_SAVE_VERSION, &raw);
    if (raw_status != ANO_RES_SAVE_OK) {
        if (info != NULL)
            *info = (anoresworld_save_info){
                .source_version = raw.format_version,
                .source_min_reader_version = raw.min_reader_version,
                .source_seq = raw.seq,
            };
        return map_raw_status(raw_status);
    }
    if (info != NULL)
        *info = (anoresworld_save_info){
            .source_version = raw.format_version,
            .source_min_reader_version = raw.min_reader_version,
            .source_seq = raw.seq,
        };

    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    ano_res_read read = {0};
    if (ano_res_reader_register(&reader) != 0)
        return ANO_RESWORLD_RESOURCE_ERROR;
    if (ano_res_read_begin(&reader, &read) != 0) {
        (void)ano_res_reader_unregister(&reader);
        return ANO_RESWORLD_RESOURCE_ERROR;
    }
    anostr_t payload = ano_res_bytes(&read, raw.resource);
    bool valid = false;
    if (raw.format_version == 1)
        valid = decode_v1(anostr_bytes(&payload), anostr_len(payload), state);
    else if (raw.format_version == ANO_RESWORLD_SAVE_VERSION)
        valid = decode_v2(anostr_bytes(&payload), anostr_len(payload), state);
    ano_res_read_end(&read);
    (void)ano_res_reader_unregister(&reader);
    (void)ano_res_unload(lifetime, raw.resource);

    if (raw.format_version > ANO_RESWORLD_SAVE_VERSION)
        return ANO_RESWORLD_UNSUPPORTED_VERSION;
    if (!valid)
        return raw.format_version < ANO_RESWORLD_SAVE_VERSION
             ? ANO_RESWORLD_MIGRATION_FAILED : ANO_RESWORLD_CORRUPT;
    if (raw.format_version < ANO_RESWORLD_SAVE_VERSION)
        return ano_resworld_save_commit(slot, state) == 0 ? ANO_RESWORLD_MIGRATED
                                                          : ANO_RESWORLD_MIGRATION_WRITE_FAILED;
    return ANO_RESWORLD_OK;
}
