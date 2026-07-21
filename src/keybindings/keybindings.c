/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Strict keybinding schema, migration, quarantine, durable replacement, and action lookup.

#include <anoptic_keybindings.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include <jsmn.h>

#define KEYBIND_TOKENS 192

typedef struct action_def {
    const char *name;
    anostr_sid id;
    int32_t key;
} action_def;

static const action_def g_actions[ANO_KEYBINDING_COUNT] = {
    { "move.forward", ANO_ACTION_MOVE_FORWARD, ANO_KEY_W },
    { "move.backward", ANO_ACTION_MOVE_BACKWARD, ANO_KEY_S },
    { "move.left", ANO_ACTION_MOVE_LEFT, ANO_KEY_A },
    { "move.right", ANO_ACTION_MOVE_RIGHT, ANO_KEY_D },
    { "move.up", ANO_ACTION_MOVE_UP, ANO_KEY_SPACE },
    { "move.down", ANO_ACTION_MOVE_DOWN, ANO_KEY_LEFT_CONTROL },
    { "menu.toggle", ANO_ACTION_MENU_TOGGLE, ANO_KEY_M },
    { "render.lighting_cycle", ANO_ACTION_LIGHTING_CYCLE, ANO_KEY_L },
    { "render.lod_finer", ANO_ACTION_LOD_FINER, ANO_KEY_LEFT_BRACKET },
    { "render.lod_coarser", ANO_ACTION_LOD_COARSER, ANO_KEY_RIGHT_BRACKET },
    { "render.shadow_lod_finer", ANO_ACTION_SHADOW_LOD_FINER, ANO_KEY_SEMICOLON },
    { "render.shadow_lod_coarser", ANO_ACTION_SHADOW_LOD_COARSER, ANO_KEY_APOSTROPHE },
    { "render.hiz_toggle", ANO_ACTION_HIZ_TOGGLE, ANO_KEY_H },
};

static ano_keybindings g_current;
static bool g_current_ready;

<<<<<<< HEAD

/* JSON tokens */

=======
>>>>>>> block-b1-base
static bool tok_eq(const char *json, const jsmntok_t *tok, const char *text)
{
    size_t n = strlen(text);
    return tok->type == JSMN_STRING && (size_t)(tok->end - tok->start) == n
        && memcmp(json + tok->start, text, n) == 0;
}

static int tok_next(const jsmntok_t *tokens, int count, int at)
{
    int end = tokens[at].end;
    at++;
    while (at < count && tokens[at].start < end)
        at++;
    return at;
}

static bool tok_i32(const char *json, const jsmntok_t *tok, int32_t *out)
{
    if (tok->type != JSMN_PRIMITIVE || tok->end <= tok->start || tok->end - tok->start >= 24)
        return false;
    char tmp[24];
    size_t n = (size_t)(tok->end - tok->start);
    memcpy(tmp, json + tok->start, n);
    tmp[n] = '\0';
    char *end = NULL;
    long v = strtol(tmp, &end, 10);
    if (end == tmp || *end != '\0' || v < INT32_MIN || v > INT32_MAX)
        return false;
    *out = (int32_t)v;
    return true;
}

static bool tok_u32(const char *json, const jsmntok_t *tok, uint32_t *out)
{
    int32_t v;
    if (!tok_i32(json, tok, &v) || v < 0)
        return false;
    *out = (uint32_t)v;
    return true;
}

static int action_index_token(const char *json, const jsmntok_t *tok)
{
    for (uint32_t i = 0; i < ANO_KEYBINDING_COUNT; i++)
        if (tok_eq(json, tok, g_actions[i].name))
            return (int)i;
    return -1;
}

<<<<<<< HEAD

/* Parse */

=======
>>>>>>> block-b1-base
static bool parse_binding_v2(const char *json, const jsmntok_t *tokens, int count, int object,
                             ano_keybinding *binding)
{
    if (tokens[object].type != JSMN_OBJECT)
        return false;
    bool key = false, mods = false;
    for (int i = object + 1; i < count && tokens[i].start < tokens[object].end; ) {
        if (tokens[i].parent != object || i + 1 >= count)
            return false;
        int value = i + 1;
        if (tok_eq(json, &tokens[i], "key"))
            key = !key && tok_i32(json, &tokens[value], &binding->key);
        else if (tok_eq(json, &tokens[i], "mods"))
            mods = !mods && tok_i32(json, &tokens[value], &binding->mods);
        else
            return false;
        i = tok_next(tokens, count, value);
    }
    return key && mods;
}

static bool parse_bindings(const char *json, const jsmntok_t *tokens, int count, int object,
                           uint32_t version, ano_keybindings *out)
{
    if (tokens[object].type != JSMN_OBJECT)
        return false;
    bool seen[ANO_KEYBINDING_COUNT] = {0};
    out->count = 0;
    for (int i = object + 1; i < count && tokens[i].start < tokens[object].end; ) {
        if (tokens[i].parent != object || i + 1 >= count || tokens[i].type != JSMN_STRING)
            return false;
        int ai = action_index_token(json, &tokens[i]);
        if (ai < 0 || seen[ai])
            return false;
        int value = i + 1;
        ano_keybinding binding = { .action = g_actions[ai].id, .mods = 0 };
        bool valid = version == 1 ? tok_i32(json, &tokens[value], &binding.key)
                                  : parse_binding_v2(json, tokens, count, value, &binding);
        if (!valid)
            return false;
        out->entries[out->count++] = binding;
        seen[ai] = true;
        i = tok_next(tokens, count, value);
    }
    if (out->count != ANO_KEYBINDING_COUNT)
        return false;
    for (uint32_t i = 0; i < ANO_KEYBINDING_COUNT; i++)
        if (!seen[i])
            return false;
    return true;
}

static bool parse_keybindings(const char *json, size_t len, ano_keybindings *out,
                              uint32_t *out_version)
{
    jsmn_parser parser;
    jsmntok_t tokens[KEYBIND_TOKENS];
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, len, tokens, KEYBIND_TOKENS);
    if (count < 1 || tokens[0].type != JSMN_OBJECT)
        return false;
    for (size_t i = (size_t)tokens[0].end; i < len; i++)
        if (json[i] != ' ' && json[i] != '\t' && json[i] != '\r' && json[i] != '\n')
            return false;

    bool schema = false, have_version = false, have_bindings = false;
    uint32_t version = 0;
    int bindings_token = -1;
    for (int i = 1; i < count && tokens[i].start < tokens[0].end; ) {
        if (tokens[i].parent != 0 || i + 1 >= count || tokens[i].type != JSMN_STRING)
            return false;
        int value = i + 1;
        if (tok_eq(json, &tokens[i], "schema"))
            schema = !schema && tok_eq(json, &tokens[value], "anoptic.keybindings");
        else if (tok_eq(json, &tokens[i], "version"))
            have_version = !have_version && tok_u32(json, &tokens[value], &version);
        else if (tok_eq(json, &tokens[i], "bindings")) {
            have_bindings = !have_bindings;
            bindings_token = value;
        } else
            return false;
        i = tok_next(tokens, count, value);
    }
    if (!schema || !have_version || !have_bindings
        || (version != 1 && version != ANO_KEYBINDINGS_VERSION))
        return false;
    if (!parse_bindings(json, tokens, count, bindings_token, version, out)
        || !ano_keybindings_validate(out))
        return false;
    *out_version = version;
    return true;
}

<<<<<<< HEAD

/* Public interface */

// Zero whole aggregate incl. 4-byte padding hole after count (exact memcmp round-trips).
=======
// The whole aggregate, padding included: ano_keybindings has a 4-byte hole after count
// (entries[] is 8-aligned for the anostr_sid), and this store is compared and hashed
// byte-wise. Zero it so two stores that agree on every field agree on every byte.
>>>>>>> block-b1-base
void ano_keybindings_defaults(ano_keybindings *bindings)
{
    if (bindings == NULL)
        return;
    memset(bindings, 0, sizeof *bindings);
    bindings->count = ANO_KEYBINDING_COUNT;
    for (uint32_t i = 0; i < ANO_KEYBINDING_COUNT; i++)
        bindings->entries[i] = (ano_keybinding){
            .action = g_actions[i].id, .key = g_actions[i].key, .mods = 0,
        };
}

bool ano_keybindings_validate(const ano_keybindings *bindings)
{
    if (bindings == NULL || bindings->count != ANO_KEYBINDING_COUNT)
        return false;
    bool seen[ANO_KEYBINDING_COUNT] = {0};
    for (uint32_t i = 0; i < bindings->count; i++) {
        const ano_keybinding *b = &bindings->entries[i];
        int ai = -1;
        for (uint32_t j = 0; j < ANO_KEYBINDING_COUNT; j++)
            if (b->action == g_actions[j].id) { ai = (int)j; break; }
        if (ai < 0 || seen[ai] || b->key < -1 || b->key > 512 || b->mods < 0 || b->mods > 15)
            return false;
        seen[ai] = true;
        if (b->key >= 0)
            for (uint32_t j = 0; j < i; j++)
                if (bindings->entries[j].key == b->key
                    && bindings->entries[j].mods == b->mods)
                    return false;
    }
    return true;
}

static const ano_keybinding *binding_for_action(const ano_keybindings *bindings, anostr_sid action)
{
    for (uint32_t i = 0; i < bindings->count; i++)
        if (bindings->entries[i].action == action)
            return &bindings->entries[i];
    return NULL;
}

int ano_keybindings_save(const ano_keybindings *bindings)
{
    if (!ano_keybindings_validate(bindings))
        return -1;
    char json[2048];
    size_t at = 0;
    int n = snprintf(json + at, sizeof json - at,
                     "{\"schema\":\"anoptic.keybindings\",\"version\":2,\"bindings\":{");
    if (n <= 0 || (size_t)n >= sizeof json - at)
        return -1;
    at += (size_t)n;
    for (uint32_t i = 0; i < ANO_KEYBINDING_COUNT; i++) {
        const ano_keybinding *b = binding_for_action(bindings, g_actions[i].id);
        n = snprintf(json + at, sizeof json - at,
                     "%s\"%s\":{\"key\":%d,\"mods\":%d}",
                     i == 0 ? "" : ",", g_actions[i].name, b->key, b->mods);
        if (n <= 0 || (size_t)n >= sizeof json - at)
            return -1;
        at += (size_t)n;
    }
    n = snprintf(json + at, sizeof json - at, "}}\n");
    if (n <= 0 || (size_t)n >= sizeof json - at)
        return -1;
    at += (size_t)n;
    return ano_res_write(ANO_KEYBINDINGS_PATH, json, at);
}

ano_keybindings_status ano_keybindings_load(ano_res_lifetime lifetime,
                                             ano_keybindings *bindings)
{
    if (bindings == NULL || lifetime.kind != ANO_RES_LIFETIME_SAVE_CONFIG)
        return ANO_KEYBINDINGS_INVALID_ARGUMENT;
    memset(bindings, 0, sizeof *bindings);      // padding too: parse_bindings fills fields, not holes
    anores_t resource = ano_res_get(lifetime, ANO_KEYBINDINGS_PATH);
    if (resource.gen == 0) {
        (void)ano_res_quarantine(ANO_KEYBINDINGS_PATH);
        ano_keybindings_defaults(bindings);
        return ano_keybindings_save(bindings) == 0 ? ANO_KEYBINDINGS_DEFAULTED
                                                   : ANO_KEYBINDINGS_IO_ERROR;
    }

    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    ano_res_read read = {0};
    uint32_t version = 0;
    bool registered = ano_res_reader_register(&reader) == 0;
    bool begun = registered && ano_res_read_begin(&reader, &read) == 0;
    bool valid = false;
    if (begun) {
        anostr_t bytes = ano_res_bytes(&read, resource);
        valid = parse_keybindings(anostr_bytes(&bytes), anostr_len(bytes), bindings, &version);
        ano_res_read_end(&read);
    }
    if (registered)
        (void)ano_res_reader_unregister(&reader);
    (void)ano_res_unload(lifetime, resource);

    if (!valid) {
        (void)ano_res_quarantine(ANO_KEYBINDINGS_PATH);
        ano_keybindings_defaults(bindings);
        return ano_keybindings_save(bindings) == 0 ? ANO_KEYBINDINGS_DEFAULTED
                                                   : ANO_KEYBINDINGS_IO_ERROR;
    }
    if (version != ANO_KEYBINDINGS_VERSION)
        return ano_keybindings_save(bindings) == 0 ? ANO_KEYBINDINGS_MIGRATED
                                                   : ANO_KEYBINDINGS_IO_ERROR;
    return ANO_KEYBINDINGS_LOADED;
}

anostr_sid ano_keybindings_action(const ano_keybindings *bindings, int32_t key, int32_t mods)
{
    if (bindings == NULL)
        return 0;
    for (uint32_t i = 0; i < bindings->count; i++)
        if (bindings->entries[i].key == key && bindings->entries[i].mods == (mods & 15))
            return bindings->entries[i].action;
    return 0;
}

void ano_keybindings_install(const ano_keybindings *bindings)
{
    if (!ano_keybindings_validate(bindings))
        return;
    g_current = *bindings;
    g_current_ready = true;
}

anostr_sid ano_keybindings_current_action(int32_t key, int32_t mods)
{
    if (!g_current_ready) {
        ano_keybindings_defaults(&g_current);
        g_current_ready = true;
    }
    return ano_keybindings_action(&g_current, key, mods);
}
